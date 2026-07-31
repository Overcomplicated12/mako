use std::env;
use std::path::PathBuf;
use std::process::{Command, ExitCode};
use std::time::Duration;

use rustytwin::compare::compare_results;
use rustytwin::module::{BuildRole, ModuleManifest};
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
        "init" => init(tail),
        "doctor" => doctor(tail),
        "replay" => replay(tail),
        "help" | "--help" | "-h" => {
            println!("{}", usage());
            Ok(0)
        }
        _ => Err(format!("unknown command {command:?}\n\n{}", usage())),
    }
}

fn check(args: &[String]) -> Result<u8, String> {
    if module_path(args).is_some() {
        return module_check(args);
    }
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

fn module_check(args: &[String]) -> Result<u8, String> {
    let manifest_path = module_path(args).expect("module path checked by caller");
    let manifest_path = PathBuf::from(manifest_path);
    let manifest = ModuleManifest::load(&manifest_path)?;
    let baseline_build = resolve_build_dir(
        &manifest,
        BuildRole::Baseline,
        optional_option(args, "--baseline-build").map(PathBuf::from),
    )?;
    let candidate_build = resolve_build_dir(
        &manifest,
        BuildRole::Candidate,
        optional_option(args, "--candidate-build").map(PathBuf::from),
    )?;
    if has_flag(args, "--build") {
        build_module_target("baseline", &baseline_build, &manifest.module.test_target)?;
        if canonical_paths_match(&baseline_build, &candidate_build) {
            println!("Reusing baseline build for candidate target.");
        } else {
            build_module_target("candidate", &candidate_build, &manifest.module.test_target)?;
        }
    }
    let baseline_test = manifest.test_path(&baseline_build);
    let candidate_test = manifest.test_path(&candidate_build);
    validate_test_binary("baseline", &baseline_test)?;
    validate_test_binary("candidate", &candidate_test)?;

    let filter = optional_option(args, "--filter")
        .map(ToOwned::to_owned)
        .or(manifest.module.filter.clone());
    let (tape_path, operations) = generated_gtest_operations(filter.as_deref())?;
    let timeout_ms = timeout_ms_with_default(args, manifest.module.timeout_ms)?;
    let output_dir = required_option(args, "--out")?;

    run_check(
        baseline_test.display().to_string(),
        candidate_test.display().to_string(),
        tape_path,
        output_dir,
        timeout_ms,
        has_flag(args, "--show-output"),
        operations,
        run_gtest_harness,
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

fn init(args: &[String]) -> Result<u8, String> {
    let manifest_path = required_option(args, "--module")?;
    let test_target = required_option(args, "--test-target")?;
    let baseline_build = optional_option(args, "--baseline-build").map(PathBuf::from);
    let candidate_build = optional_option(args, "--candidate-build").map(PathBuf::from);
    let filter = optional_option(args, "--filter").map(ToOwned::to_owned);
    let timeout_ms = optional_timeout_ms(args)?;

    ModuleManifest::create(
        &PathBuf::from(&manifest_path),
        test_target,
        baseline_build,
        candidate_build,
        filter,
        timeout_ms,
    )?;
    println!("Created module manifest:\n  {manifest_path}");
    Ok(0)
}

fn doctor(args: &[String]) -> Result<u8, String> {
    let manifest_path = required_option(args, "--module")?;
    let manifest_path = PathBuf::from(manifest_path);
    let manifest = ModuleManifest::load(&manifest_path)?;
    let baseline_build = resolve_build_dir(
        &manifest,
        BuildRole::Baseline,
        optional_option(args, "--baseline-build").map(PathBuf::from),
    )?;
    let candidate_build = resolve_build_dir(
        &manifest,
        BuildRole::Candidate,
        optional_option(args, "--candidate-build").map(PathBuf::from),
    )?;

    let mut errors = Vec::new();
    let mut warnings = Vec::new();
    let mut checks = vec![format!("module manifest: {}", manifest_path.display())];
    match tool_version("clang++-22", "--version") {
        Ok(version) => checks.push(format!("clang++-22: {version}")),
        Err(error) => errors.push(error),
    }
    match tool_version("cmake", "--version") {
        Ok(version) => checks.push(format!("cmake: {version}")),
        Err(error) => errors.push(error),
    }

    check_build_dir(
        "baseline",
        &baseline_build,
        &manifest.module.test_target,
        &mut checks,
        &mut errors,
    );
    check_build_dir(
        "candidate",
        &candidate_build,
        &manifest.module.test_target,
        &mut checks,
        &mut errors,
    );
    if canonical_paths_match(&baseline_build, &candidate_build) {
        warnings.push(
            "baseline and candidate resolve to the same build directory; this is an identity smoke check, not migration evidence"
                .to_owned(),
        );
    }

    for check in checks {
        println!("ok: {check}");
    }
    for warning in warnings {
        println!("warning: {warning}");
    }
    if errors.is_empty() {
        println!("RustyTwin doctor: PASSED");
        Ok(0)
    } else {
        for error in errors {
            println!("error: {error}");
        }
        println!("RustyTwin doctor: FAILED");
        Ok(1)
    }
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
        (None, filter) => generated_gtest_operations(filter),
    }
}

fn generated_gtest_operations(
    filter: Option<&str>,
) -> Result<(String, Vec<rustytwin::protocol::Operation>), String> {
    match filter {
        Some(filter) if filter.trim().is_empty() => Err("--filter must not be empty".to_owned()),
        Some(filter) => Ok((
            format!("<generated GoogleTest filter: {filter}>"),
            vec![rustytwin::protocol::Operation {
                kind: "operation".to_owned(),
                step: 1,
                op: "run_gtest".to_owned(),
                args: serde_json::json!({"gtest_filter": filter}),
            }],
        )),
        None => Ok((
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
    timeout_ms_with_default(args, None)
}

fn timeout_ms_with_default(args: &[String], manifest_default: Option<u64>) -> Result<u64, String> {
    optional_timeout_ms(args).map(|value| value.or(manifest_default).unwrap_or(DEFAULT_TIMEOUT_MS))
}

fn optional_timeout_ms(args: &[String]) -> Result<Option<u64>, String> {
    optional_option(args, "--timeout-ms")
        .map(|value| {
            value
                .parse::<u64>()
                .map_err(|_| "--timeout-ms must be an unsigned integer".to_owned())
        })
        .transpose()
}

fn module_path(args: &[String]) -> Option<&str> {
    optional_option(args, "--module").or_else(|| {
        args.first()
            .filter(|value| !value.starts_with('-'))
            .map(String::as_str)
    })
}

fn resolve_build_dir(
    manifest: &ModuleManifest,
    role: BuildRole,
    override_dir: Option<PathBuf>,
) -> Result<PathBuf, String> {
    manifest.build_dir(role, override_dir)
}

fn validate_test_binary(role: &str, path: &std::path::Path) -> Result<(), String> {
    if path.is_file() {
        Ok(())
    } else {
        Err(format!(
            "{role} test target is missing: {} (build the target first)",
            path.display()
        ))
    }
}

fn build_module_target(
    role: &str,
    build_dir: &std::path::Path,
    target: &str,
) -> Result<(), String> {
    println!("Building {role} target {target}...");
    let output = Command::new("cmake")
        .arg("--build")
        .arg(build_dir)
        .arg("--target")
        .arg(target)
        .output()
        .map_err(|error| {
            format!(
                "could not start {role} build in {}: {error}",
                build_dir.display()
            )
        })?;
    if output.status.success() {
        println!("Built {role} target {target}.");
        return Ok(());
    }
    Err(format!(
        "{role} build failed for target {target} in {}:\n{}",
        build_dir.display(),
        command_diagnostics(&output)
    ))
}

fn command_diagnostics(output: &std::process::Output) -> String {
    let stderr = String::from_utf8_lossy(&output.stderr);
    let stdout = String::from_utf8_lossy(&output.stdout);
    let diagnostics = if stderr.trim().is_empty() {
        stdout.as_ref()
    } else {
        stderr.as_ref()
    };
    let lines: Vec<_> = diagnostics.lines().rev().take(20).collect();
    if lines.is_empty() {
        return format!("cmake exited with status {}", output.status);
    }
    lines.into_iter().rev().collect::<Vec<_>>().join("\n")
}

fn tool_version(program: &str, argument: &str) -> Result<String, String> {
    let output = Command::new(program)
        .arg(argument)
        .output()
        .map_err(|error| format!("could not run {program}: {error}"))?;
    if !output.status.success() {
        return Err(format!("{program} {argument} exited unsuccessfully"));
    }
    String::from_utf8_lossy(&output.stdout)
        .lines()
        .next()
        .map(ToOwned::to_owned)
        .ok_or_else(|| format!("{program} {argument} produced no output"))
}

fn check_build_dir(
    role: &str,
    build_dir: &std::path::Path,
    test_target: &str,
    checks: &mut Vec<String>,
    errors: &mut Vec<String>,
) {
    if !build_dir.is_dir() {
        errors.push(format!(
            "{role} build directory does not exist: {}",
            build_dir.display()
        ));
        return;
    }
    checks.push(format!("{role} build directory: {}", build_dir.display()));
    let test_path = build_dir.join(test_target);
    if test_path.is_file() {
        checks.push(format!("{role} test target: {}", test_path.display()));
    } else {
        errors.push(format!(
            "{role} test target is missing: {} (build {test_target} first)",
            test_path.display()
        ));
    }
}

fn canonical_paths_match(left: &std::path::Path, right: &std::path::Path) -> bool {
    match (left.canonicalize(), right.canonicalize()) {
        (Ok(left), Ok(right)) => left == right,
        _ => left == right,
    }
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
    "Usage:\n  rustytwin init --module <path> --test-target <target> [--baseline-build <dir>] [--candidate-build <dir>] [--filter <gtest-filter>] [--timeout-ms <ms>]\n  rustytwin doctor --module <path> [--baseline-build <dir>] [--candidate-build <dir>]\n  rustytwin check --module <path> --out <dir> [--build] [--baseline-build <dir>] [--candidate-build <dir>] [--filter <gtest-filter>] [--timeout-ms <ms>] [--show-output]\n  rustytwin check <module-path> --out <dir> [module check options]\n  rustytwin check --baseline-bin <path> --candidate-bin <path> --tape <path> --out <dir> [--timeout-ms <ms>] [--show-output]\n  rustytwin gtest-check --baseline-test <path> --candidate-test <path> --out <dir> [--filter <gtest-filter> | --tape <path>] [--timeout-ms <ms>] [--show-output]\n  rustytwin replay <artifact>".to_owned()
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

    #[test]
    fn positional_module_path_is_recognized() {
        let args = vec!["raft-quorum.toml".to_owned()];
        assert_eq!(module_path(&args), Some("raft-quorum.toml"));
    }

    #[test]
    fn module_timeout_uses_the_manifest_default() {
        assert_eq!(timeout_ms_with_default(&[], Some(123)).unwrap(), 123);
    }
}
