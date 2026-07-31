//! Harness execution and NDJSON adaptation.
//!
//! The generic runner communicates with custom harnesses over standard input
//! and output. The GoogleTest adapter instead executes one filtered test per
//! operation and synthesizes a comparable event trace from each exit status.

use std::fs;
use std::io::{Read, Write};
use std::path::Path;
use std::process::{Command, ExitStatus, Stdio};
use std::thread;
use std::time::{Duration, Instant};

use crate::protocol::{ExitStatusInfo, HarnessEvent, HarnessResult, Operation};

/// Per-harness execution limits.
#[derive(Clone, Copy, Debug)]
pub struct RunnerConfig {
    /// Maximum wall-clock time allowed for one child process.
    pub timeout: Duration,
}

impl Default for RunnerConfig {
    fn default() -> Self {
        Self {
            timeout: Duration::from_secs(5),
        }
    }
}

/// Parse a tape file as one [`Operation`] JSON object per non-empty line.
pub fn load_tape(path: &Path) -> Result<Vec<Operation>, String> {
    let content = fs::read_to_string(path)
        .map_err(|error| format!("could not read tape {}: {error}", path.display()))?;
    parse_tape(&content)
}

/// Parse NDJSON operation content without reading from the filesystem.
pub fn parse_tape(content: &str) -> Result<Vec<Operation>, String> {
    content
        .lines()
        .enumerate()
        .filter_map(|(index, line)| (!line.trim().is_empty()).then_some((index + 1, line)))
        .map(|(line_number, line)| {
            serde_json::from_str::<Operation>(line)
                .map_err(|error| format!("invalid operation at tape line {line_number}: {error}"))
        })
        .collect()
}

/// Encode operations in the newline-delimited form expected by custom harnesses.
pub fn tape_to_ndjson(operations: &[Operation]) -> Result<String, String> {
    let mut output = String::new();
    for operation in operations {
        let line = serde_json::to_string(operation)
            .map_err(|error| format!("could not encode operation tape: {error}"))?;
        output.push_str(&line);
        output.push('\n');
    }
    Ok(output)
}

/// Run one custom harness with a complete operation tape.
///
/// Harness output is retained even when the process exits non-zero so the
/// comparator can explain which side failed first.
pub fn run_harness(
    executable: &Path,
    operations: &[Operation],
    config: RunnerConfig,
) -> Result<HarnessResult, String> {
    let tape = tape_to_ndjson(operations)?;
    let process = run_process(executable, &[], Some(tape.as_bytes()), config)?;
    let stdout = String::from_utf8_lossy(&process.stdout)
        .trim_end()
        .to_owned();
    let stderr = String::from_utf8_lossy(&process.stderr).to_string();
    let (events, parse_error) = parse_events(&stdout);

    Ok(HarnessResult {
        events,
        stdout,
        stderr,
        exit_status: exit_status_info(process.status),
        timed_out: process.timed_out,
        parse_error,
    })
}

/// Run a GoogleTest binary once for every tape operation.
///
/// An operation may set `args.gtest_filter`; otherwise the full binary runs.
/// GoogleTest's human-readable output is retained as diagnostics while
/// RustyTwin synthesizes the NDJSON event trace from each exit status.
pub fn run_gtest_harness(
    executable: &Path,
    operations: &[Operation],
    config: RunnerConfig,
) -> Result<HarnessResult, String> {
    let mut events = Vec::with_capacity(operations.len());
    let mut stdout_lines = Vec::with_capacity(operations.len());
    let mut stderr = String::new();
    let mut exit_status = ExitStatusInfo {
        success: true,
        code: Some(0),
        signal: None,
    };
    let mut timed_out = false;

    for operation in operations {
        let filter = gtest_filter(operation)?;
        let mut arguments = vec!["--gtest_color=no".to_owned()];
        if let Some(filter) = &filter {
            arguments.push(format!("--gtest_filter={filter}"));
        }
        let process = run_process(executable, &arguments, None, config)?;
        let status = exit_status_info(process.status);
        let passed = status.success && !process.timed_out;
        let event = gtest_event(operation, passed, filter);
        stdout_lines.push(
            serde_json::to_string(&event)
                .map_err(|error| format!("could not encode GoogleTest event: {error}"))?,
        );
        events.push(event);

        append_gtest_output(
            &mut stderr,
            operation.step,
            &process.stdout,
            &process.stderr,
        );
        if exit_status.success && !passed {
            exit_status = status;
        }
        if process.timed_out {
            timed_out = true;
            break;
        }
    }

    Ok(HarnessResult {
        events,
        stdout: stdout_lines.join("\n"),
        stderr,
        exit_status,
        timed_out,
        parse_error: None,
    })
}

struct ProcessResult {
    stdout: Vec<u8>,
    stderr: Vec<u8>,
    status: ExitStatus,
    timed_out: bool,
}

fn run_process(
    executable: &Path,
    arguments: &[String],
    input: Option<&[u8]>,
    config: RunnerConfig,
) -> Result<ProcessResult, String> {
    let mut command = Command::new(executable);
    command
        .args(arguments)
        .stdout(Stdio::piped())
        .stderr(Stdio::piped());
    if input.is_some() {
        command.stdin(Stdio::piped());
    } else {
        command.stdin(Stdio::null());
    }
    let mut child = command
        .spawn()
        .map_err(|error| format!("could not launch {}: {error}", executable.display()))?;

    if let Some(input) = input {
        let mut stdin = child.stdin.take().expect("stdin was configured as piped");
        stdin.write_all(input).map_err(|error| {
            format!(
                "could not write operation tape to {}: {error}",
                executable.display()
            )
        })?;
    }
    // Drain both pipes concurrently so a verbose child cannot block while the
    // parent waits for its process status.
    let mut stdout = child.stdout.take().expect("stdout was configured as piped");
    let mut stderr = child.stderr.take().expect("stderr was configured as piped");
    let stdout_reader = thread::spawn(move || {
        let mut bytes = Vec::new();
        stdout.read_to_end(&mut bytes).map(|_| bytes)
    });
    let stderr_reader = thread::spawn(move || {
        let mut bytes = Vec::new();
        stderr.read_to_end(&mut bytes).map(|_| bytes)
    });

    let deadline = Instant::now() + config.timeout;
    let mut timed_out = false;
    let status = loop {
        if let Some(status) = child
            .try_wait()
            .map_err(|error| format!("could not wait for {}: {error}", executable.display()))?
        {
            break status;
        }

        if Instant::now() >= deadline {
            // Reap after killing so the child cannot outlive the check.
            timed_out = true;
            let _ = child.kill();
            break child.wait().map_err(|error| {
                format!("could not reap timed-out {}: {error}", executable.display())
            })?;
        }

        thread::sleep(Duration::from_millis(5));
    };

    let stdout = stdout_reader
        .join()
        .map_err(|_| "stdout reader panicked".to_owned())?
        .map_err(|error| format!("could not read harness stdout: {error}"))?;
    let stderr = stderr_reader
        .join()
        .map_err(|_| "stderr reader panicked".to_owned())?
        .map_err(|error| format!("could not read harness stderr: {error}"))?;

    Ok(ProcessResult {
        stdout,
        stderr,
        status,
        timed_out,
    })
}

fn gtest_filter(operation: &Operation) -> Result<Option<String>, String> {
    let Some(filter) = operation.args.get("gtest_filter") else {
        return Ok(None);
    };
    filter.as_str().map(str::to_owned).map(Some).ok_or_else(|| {
        format!(
            "operation step {} has a non-string args.gtest_filter",
            operation.step
        )
    })
}

fn gtest_event(operation: &Operation, passed: bool, filter: Option<String>) -> HarnessEvent {
    let mut fields = std::collections::BTreeMap::new();
    fields.insert("value".to_owned(), serde_json::Value::Bool(passed));
    if let Some(filter) = filter {
        fields.insert("gtest_filter".to_owned(), serde_json::Value::String(filter));
    }
    HarnessEvent {
        kind: "event".to_owned(),
        step: operation.step,
        event: "gtest_result".to_owned(),
        fields,
    }
}

fn append_gtest_output(stderr: &mut String, step: u64, stdout: &[u8], process_stderr: &[u8]) {
    stderr.push_str(&format!("GoogleTest step {step}:\n"));
    stderr.push_str(&String::from_utf8_lossy(stdout));
    stderr.push_str(&String::from_utf8_lossy(process_stderr));
    if !stderr.ends_with('\n') {
        stderr.push('\n');
    }
}

fn parse_events(stdout: &str) -> (Vec<HarnessEvent>, Option<String>) {
    let mut events = Vec::new();
    for (index, line) in stdout.lines().enumerate() {
        if line.trim().is_empty() {
            continue;
        }
        match serde_json::from_str::<HarnessEvent>(line) {
            Ok(event) => events.push(event),
            Err(error) => {
                return (
                    events,
                    Some(format!(
                        "invalid harness event at stdout line {}: {error}",
                        index + 1
                    )),
                )
            }
        }
    }
    (events, None)
}

fn exit_status_info(status: std::process::ExitStatus) -> ExitStatusInfo {
    #[cfg(unix)]
    use std::os::unix::process::ExitStatusExt;

    ExitStatusInfo {
        success: status.success(),
        code: status.code(),
        #[cfg(unix)]
        signal: status.signal(),
        #[cfg(not(unix))]
        signal: None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_non_empty_ndjson_lines() {
        let tape = parse_tape("\n{\"kind\":\"operation\",\"step\":1,\"op\":\"majority_count\"}\n")
            .unwrap();

        assert_eq!(tape.len(), 1);
        assert_eq!(tape[0].op, "majority_count");
    }

    #[test]
    fn rejects_invalid_tape_lines() {
        let error = parse_tape("not-json\n").unwrap_err();
        assert!(error.contains("tape line 1"));
    }

    #[test]
    fn event_parser_keeps_prior_events_when_later_output_is_invalid() {
        let (events, error) = parse_events(
            "{\"kind\":\"event\",\"step\":1,\"event\":\"return\",\"value\":3}\ninvalid\n",
        );

        assert_eq!(events.len(), 1);
        assert!(error.unwrap().contains("stdout line 2"));
    }

    #[test]
    fn gtest_filter_comes_from_operation_arguments() {
        let operation = Operation {
            kind: "operation".to_owned(),
            step: 4,
            op: "run_test".to_owned(),
            args: serde_json::json!({"gtest_filter": "Suite.Case"}),
        };

        assert_eq!(
            gtest_filter(&operation).unwrap(),
            Some("Suite.Case".to_owned())
        );
        let event = gtest_event(&operation, true, Some("Suite.Case".to_owned()));
        assert_eq!(event.fields.get("value"), Some(&serde_json::json!(true)));
    }
}
