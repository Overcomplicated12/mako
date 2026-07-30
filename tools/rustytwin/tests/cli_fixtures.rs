use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::{Command, Output};

fn tool_root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
}

fn fixture(profile: &str, role: &str) -> PathBuf {
    tool_root()
        .join("fixtures")
        .join(profile)
        .join(format!("{role}.sh"))
}

fn tape() -> PathBuf {
    tool_root().join("examples/simple_tape.ndjson")
}

fn gtest_fixture(name: &str) -> PathBuf {
    tool_root()
        .join("fixtures/gtest")
        .join(format!("{name}.sh"))
}

fn gtest_tape() -> PathBuf {
    tool_root().join("examples/gtest_smoke.ndjson")
}

fn output_dir(name: &str) -> PathBuf {
    env::temp_dir().join(format!("rustytwin-cli-{name}-{}", std::process::id()))
}

fn run_check(profile: &str, timeout_ms: u64, output_dir: &Path) -> Output {
    let _ = fs::remove_dir_all(output_dir);
    Command::new(env!("CARGO_BIN_EXE_rustytwin"))
        .args([
            "check",
            "--baseline-bin",
            fixture(profile, "baseline").to_str().unwrap(),
            "--candidate-bin",
            fixture(profile, "candidate").to_str().unwrap(),
            "--tape",
            tape().to_str().unwrap(),
            "--out",
            output_dir.to_str().unwrap(),
            "--timeout-ms",
            &timeout_ms.to_string(),
        ])
        .output()
        .unwrap()
}

fn run_gtest_check(baseline: &str, candidate: &str, output_dir: &Path) -> Output {
    let _ = fs::remove_dir_all(output_dir);
    Command::new(env!("CARGO_BIN_EXE_rustytwin"))
        .args([
            "gtest-check",
            "--baseline-test",
            gtest_fixture(baseline).to_str().unwrap(),
            "--candidate-test",
            gtest_fixture(candidate).to_str().unwrap(),
            "--tape",
            gtest_tape().to_str().unwrap(),
            "--out",
            output_dir.to_str().unwrap(),
            "--timeout-ms",
            "500",
        ])
        .output()
        .unwrap()
}

fn stdout(output: &Output) -> String {
    String::from_utf8_lossy(&output.stdout).to_string()
}

#[test]
fn equivalent_fixture_passes_despite_elapsed_metadata() {
    let output_dir = output_dir("equivalent");
    let output = run_check("equivalent", 500, &output_dir);

    assert!(output.status.success(), "{}", stdout(&output));
    assert!(stdout(&output).contains("Behavioral migration check: PASSED"));
    assert!(!output_dir.exists());
}

#[test]
fn return_mismatch_writes_an_artifact_and_replays_it() {
    let output_dir = output_dir("return-mismatch");
    let output = run_check("return_mismatch", 500, &output_dir);

    assert_eq!(output.status.code(), Some(1));
    assert!(stdout(&output).contains("First divergence at step 2"));
    let artifact = output_dir.join("rustytwin-failure-0001.json");
    assert!(artifact.exists());

    let replay = Command::new(env!("CARGO_BIN_EXE_rustytwin"))
        .args(["replay", artifact.to_str().unwrap()])
        .output()
        .unwrap();
    assert!(replay.status.success());
    assert!(stdout(&replay).contains("Replay artifact version"));
    let _ = fs::remove_dir_all(output_dir);
}

#[test]
fn state_mismatch_reports_the_event_difference() {
    let output_dir = output_dir("state-mismatch");
    let output = run_check("state_mismatch", 500, &output_dir);

    assert_eq!(output.status.code(), Some(1));
    assert!(stdout(&output).contains("First divergence at step 2"));
    assert!(stdout(&output).contains("harness events differ"));
    let _ = fs::remove_dir_all(output_dir);
}

#[test]
fn candidate_crash_is_reported_as_an_exit_status_difference() {
    let output_dir = output_dir("crash-mismatch");
    let output = run_check("crash_mismatch", 500, &output_dir);

    assert_eq!(output.status.code(), Some(1));
    assert!(stdout(&output).contains("harness exit statuses differ"));
    assert!(output_dir.join("rustytwin-failure-0001.json").exists());
    let _ = fs::remove_dir_all(output_dir);
}

#[test]
fn candidate_timeout_is_reported_clearly() {
    let output_dir = output_dir("timeout-mismatch");
    // Leave enough startup headroom for the Python baseline when Cargo runs
    // integration tests in parallel; the candidate sleeps for five seconds.
    let output = run_check("timeout_mismatch", 200, &output_dir);

    assert_eq!(output.status.code(), Some(1));
    assert!(stdout(&output).contains("only one harness timed out"));
    let _ = fs::remove_dir_all(output_dir);
}

#[test]
fn gtest_check_compares_passing_test_binaries() {
    let output_dir = output_dir("gtest-passing");
    let output = run_gtest_check("passing", "passing", &output_dir);

    assert!(output.status.success(), "{}", stdout(&output));
    assert!(stdout(&output).contains("Behavioral migration check: PASSED"));
}

#[test]
fn gtest_check_writes_an_artifact_when_a_test_binary_fails() {
    let output_dir = output_dir("gtest-failing");
    let output = run_gtest_check("passing", "failing", &output_dir);

    assert_eq!(output.status.code(), Some(1));
    assert!(stdout(&output).contains("harness exit statuses differ"));
    assert!(output_dir.join("rustytwin-failure-0001.json").exists());
    let _ = fs::remove_dir_all(output_dir);
}
