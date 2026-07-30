use std::env;
use std::path::PathBuf;
use std::process::ExitCode;
use std::time::Duration;

use rustytwin::compare::compare_results;
use rustytwin::protocol::CheckMetadata;
use rustytwin::replay::{load_artifact, save_failure};
use rustytwin::report::{render_artifact, render_comparison};
use rustytwin::runner::{load_tape, run_harness, RunnerConfig};

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
    let timeout_ms = optional_option(args, "--timeout-ms")
        .map(|value| {
            value
                .parse::<u64>()
                .map_err(|_| "--timeout-ms must be an unsigned integer".to_owned())
        })
        .transpose()?
        .unwrap_or(DEFAULT_TIMEOUT_MS);

    let tape_path = PathBuf::from(tape_path);
    let operations = load_tape(&tape_path)?;
    let config = RunnerConfig {
        timeout: Duration::from_millis(timeout_ms),
    };
    let baseline = run_harness(&PathBuf::from(&baseline_bin), &operations, config)?;
    let candidate = run_harness(&PathBuf::from(&candidate_bin), &operations, config)?;
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

fn usage() -> String {
    "Usage:\n  rustytwin check --baseline-bin <path> --candidate-bin <path> --tape <path> --out <dir> [--timeout-ms <ms>]\n  rustytwin replay <artifact>".to_owned()
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
}
