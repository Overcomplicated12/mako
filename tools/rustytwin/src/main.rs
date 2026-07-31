use std::env;
use std::path::PathBuf;
use std::process::ExitCode;
use std::time::Duration;

use rustytwin::compare::compare_results;
use rustytwin::protocol::CheckMetadata;
use rustytwin::replay::{load_artifact, save_failure};
use rustytwin::report::{render_artifact, render_comparison};
use rustytwin::runner::{load_tape, run_gtest_harness, run_harness, RunnerConfig};

const DEFAULT_TIMEOUT_MS: u64 = 5_000;

fn main() -> ExitCode {
    match run(env::args().skip(1).collect()) {
        Ok(code) => ExitCode::from(code),
        Err(error) => {
            eprintln!("rustytwin: {error}");
            ExitCode::from(2)
        }
    }
}

fn run(args: Vec<String>) -> Result<u8, String> {
    let Some((command, tail)) = args.split_first() else {
        return Err(usage());
    };
    match command.as_str() {
        "check" => check(tail),
        "gtest-check" => gtest_check(tail),
        "replay" => replay(tail),
        "help" | "--help" | "-h" => {
            println!("{}", usage());
            Ok(0)
        }
        _ => Err(format!("unknown command {command:?}\n\n{}", usage())),
    }
}

fn check(args: &[String]) -> Result<u8, String> {
    let baseline_bin = required_option(args, "--baseline-bin")?;
    let candidate_bin = required_option(args, "--candidate-bin")?;
    let tape_path = required_option(args, "--tape")?;
    let output_dir = required_option(args, "--out")?;
    let timeout_ms = timeout_ms(args)?;
    let operations = load_tape(&PathBuf::from(&tape_path))?;

    run_check(
        baseline_bin,
        candidate_bin,
        tape_path,
        output_dir,
        timeout_ms,
        has_flag(args, "--show-output"),
        operations,
        run_harness,
    )
}

fn gtest_check(args: &[String]) -> Result<u8, String> {
    let baseline_test = required_option(args, "--baseline-test")?;
    let candidate_test = required_option(args, "--candidate-test")?;
    let output_dir = required_option(args, "--out")?;
    let timeout_ms = timeout_ms(args)?;
    let (tape_path, operations) = gtest_operations(args)?;

    run_check(
        baseline_test,
        candidate_test,
        tape_path,
        output_dir,
        timeout_ms,
        has_flag(args, "--show-output"),
        operations,
        run_gtest_harness,
    )
}

fn run_check(
    baseline_bin: String,
    candidate_bin: String,
    tape_path: String,
    output_dir: String,
    timeout_ms: u64,
    show_output: bool,
    operations: Vec<rustytwin::protocol::Operation>,
    runner: fn(
        &std::path::Path,
        &[rustytwin::protocol::Operation],
        RunnerConfig,
    ) -> Result<rustytwin::protocol::HarnessResult, String>,
) -> Result<u8, String> {
    let tape_path = PathBuf::from(tape_path);
    let config = RunnerConfig {
        timeout: Duration::from_millis(timeout_ms),
    };
    let baseline = runner(&PathBuf::from(&baseline_bin), &operations, config)?;
    let candidate = runner(&PathBuf::from(&candidate_bin), &operations, config)?;
    if show_output {
        print_captured_output("Baseline", &baseline);
        print_captured_output("Candidate", &candidate);
    }
    let comparison = compare_results(&baseline, &candidate);

    println!("{}", render_comparison(&comparison));
    if comparison.passed {
        return Ok(0);
    }

    let artifact_path = save_failure(
        &PathBuf::from(output_dir),
        CheckMetadata {
            baseline_bin,
            candidate_bin,
            tape_path: tape_path.display().to_string(),
            timeout_ms,
        },
        operations,
        baseline,
        candidate,
        comparison,
    )?;
    println!("\nReplay artifact:\n  {}", artifact_path.display());
    Ok(1)
}

fn gtest_operations(
    args: &[String],
) -> Result<(String, Vec<rustytwin::protocol::Operation>), String> {
    let tape = optional_option(args, "--tape");
    let filter = optional_option(args, "--filter");
    match (tape, filter) {
        (Some(_), Some(_)) => {
            Err("gtest-check accepts either --tape or --filter, not both".to_owned())
        }
        (Some(tape), None) => Ok((tape.to_owned(), load_tape(&PathBuf::from(tape))?)),
        (None, Some(filter)) if filter.is_empty() => Err("--filter must not be empty".to_owned()),
        (None, Some(filter)) => Ok((
            format!("<generated GoogleTest filter: {filter}>"),
            vec![rustytwin::protocol::Operation {
                kind: "operation".to_owned(),
                step: 1,
                op: "run_gtest".to_owned(),
                args: serde_json::json!({"gtest_filter": filter}),
            }],
        )),
        (None, None) => Ok((
            "<generated full GoogleTest suite>".to_owned(),
            vec![rustytwin::protocol::Operation {
                kind: "operation".to_owned(),
                step: 1,
                op: "run_gtest".to_owned(),
                args: serde_json::json!({}),
            }],
        )),
    }
}

fn print_captured_output(label: &str, result: &rustytwin::protocol::HarnessResult) {
    println!("\n{label} captured output:");
    if result.stdout.is_empty() && result.stderr.is_empty() {
        println!("  <no output>");
        return;
    }
    if !result.stdout.is_empty() {
        println!("stdout:");
        print_output(&result.stdout);
    }
    if !result.stderr.is_empty() {
        println!("stderr:");
        print_output(&result.stderr);
    }
}

fn print_output(output: &str) {
    print!("{output}");
    if !output.ends_with('\n') {
        println!();
    }
}

fn timeout_ms(args: &[String]) -> Result<u64, String> {
    optional_option(args, "--timeout-ms")
        .map(|value| {
            value
                .parse::<u64>()
                .map_err(|_| "--timeout-ms must be an unsigned integer".to_owned())
        })
        .transpose()
        .map(|value| value.unwrap_or(DEFAULT_TIMEOUT_MS))
}

fn replay(args: &[String]) -> Result<u8, String> {
    if args.len() != 1 {
        return Err("replay requires exactly one artifact path".to_owned());
    }
    let artifact = load_artifact(&PathBuf::from(&args[0]))?;
    println!("{}", render_artifact(&artifact));
    Ok(0)
}

fn required_option(args: &[String], name: &str) -> Result<String, String> {
    optional_option(args, name)
        .map(ToOwned::to_owned)
        .ok_or_else(|| format!("check requires {name}"))
}

fn optional_option<'a>(args: &'a [String], name: &str) -> Option<&'a str> {
    args.windows(2)
        .find_map(|pair| (pair[0] == name).then_some(pair[1].as_str()))
}

fn has_flag(args: &[String], flag: &str) -> bool {
    args.iter().any(|argument| argument == flag)
}

fn usage() -> String {
    "Usage:\n  rustytwin check --baseline-bin <path> --candidate-bin <path> --tape <path> --out <dir> [--timeout-ms <ms>] [--show-output]\n  rustytwin gtest-check --baseline-test <path> --candidate-test <path> --out <dir> [--filter <gtest-filter> | --tape <path>] [--timeout-ms <ms>] [--show-output]\n  rustytwin replay <artifact>".to_owned()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_required_options() {
        let args = vec!["--baseline-bin".to_owned(), "baseline".to_owned()];
        assert_eq!(
            required_option(&args, "--baseline-bin").unwrap(),
            "baseline"
        );
    }

    #[test]
    fn rejects_unknown_commands() {
        let error = run(vec!["unknown".to_owned()]).unwrap_err();
        assert!(error.contains("unknown command"));
    }

    #[test]
    fn recognizes_show_output_flag() {
        assert!(has_flag(&["--show-output".to_owned()], "--show-output"));
    }

    #[test]
    fn gtest_filter_creates_a_single_operation() {
        let args = vec!["--filter".to_owned(), "Suite.Case".to_owned()];
        let (source, operations) = gtest_operations(&args).unwrap();

        assert!(source.contains("Suite.Case"));
        assert_eq!(operations.len(), 1);
        assert_eq!(operations[0].args["gtest_filter"], "Suite.Case");
    }

    #[test]
    fn gtest_filter_and_tape_are_mutually_exclusive() {
        let args = vec![
            "--filter".to_owned(),
            "Suite.Case".to_owned(),
            "--tape".to_owned(),
            "tape.ndjson".to_owned(),
        ];

        assert!(gtest_operations(&args).unwrap_err().contains("not both"));
    }
}
