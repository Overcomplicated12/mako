use std::fs;
use std::io::{Read, Write};
use std::path::Path;
use std::process::{Command, Stdio};
use std::thread;
use std::time::{Duration, Instant};

use crate::protocol::{ExitStatusInfo, HarnessEvent, HarnessResult, Operation};

#[derive(Clone, Copy, Debug)]
pub struct RunnerConfig {
    pub timeout: Duration,
}

impl Default for RunnerConfig {
    fn default() -> Self {
        Self {
            timeout: Duration::from_secs(5),
        }
    }
}

/// Parse a tape file as one `Operation` JSON object per non-empty line.
pub fn load_tape(path: &Path) -> Result<Vec<Operation>, String> {
    let content = fs::read_to_string(path)
        .map_err(|error| format!("could not read tape {}: {error}", path.display()))?;
    parse_tape(&content)
}

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

/// Run one harness with a complete operation tape. Harness output is retained
/// even when the process exits non-zero so the comparator can explain which
/// side failed first.
pub fn run_harness(
    executable: &Path,
    operations: &[Operation],
    config: RunnerConfig,
) -> Result<HarnessResult, String> {
    let tape = tape_to_ndjson(operations)?;
    let mut child = Command::new(executable)
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .map_err(|error| format!("could not launch {}: {error}", executable.display()))?;

    let mut stdin = child.stdin.take().expect("stdin was configured as piped");
    stdin.write_all(tape.as_bytes()).map_err(|error| {
        format!(
            "could not write operation tape to {}: {error}",
            executable.display()
        )
    })?;
    drop(stdin);

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

    let stdout = String::from_utf8_lossy(&stdout).trim_end().to_owned();
    let stderr = String::from_utf8_lossy(&stderr).to_string();
    let (events, parse_error) = parse_events(&stdout);

    Ok(HarnessResult {
        events,
        stdout,
        stderr,
        exit_status: exit_status_info(status),
        timed_out,
        parse_error,
    })
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
}
