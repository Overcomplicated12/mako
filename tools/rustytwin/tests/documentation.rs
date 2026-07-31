use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

use rustytwin::module::ModuleManifest;
use rustytwin::runner::load_tape;

fn tool_root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
}

fn read_document(path: impl AsRef<Path>) -> String {
    fs::read_to_string(path).unwrap()
}

fn fenced_block_after(document: &str, heading: &str, language: &str) -> String {
    let section = document
        .split_once(heading)
        .unwrap_or_else(|| panic!("missing documentation heading {heading:?}"))
        .1;
    let opening = format!("```{language}\n");
    let block = section
        .split_once(&opening)
        .unwrap_or_else(|| panic!("missing {language} block after {heading:?}"))
        .1;
    block
        .split_once("\n```")
        .unwrap_or_else(|| panic!("unterminated {language} block after {heading:?}"))
        .0
        .to_owned()
}

#[test]
fn command_reference_matches_cli_help() {
    let readme = read_document(tool_root().join("README.md"));
    let documented_help = fenced_block_after(&readme, "## Command Reference", "text");
    let output = Command::new(env!("CARGO_BIN_EXE_rustytwin"))
        .arg("--help")
        .output()
        .unwrap();

    assert!(output.status.success());
    assert_eq!(
        documented_help.trim(),
        String::from_utf8_lossy(&output.stdout).trim()
    );
}

#[test]
fn module_manifest_example_parses_and_validates() {
    let readme = read_document(tool_root().join("README.md"));
    let manifest = fenced_block_after(&readme, "## Module Manifest", "toml");
    let manifest: ModuleManifest = toml::from_str(&manifest).unwrap();

    manifest.validate().unwrap();
}

#[test]
fn shipped_operation_tapes_parse() {
    for name in [
        "simple_tape.ndjson",
        "gtest_smoke.ndjson",
        "raft_helpers_tape.ndjson",
        "raft_quorum_smoke.ndjson",
    ] {
        let tape = load_tape(&tool_root().join("examples").join(name)).unwrap();
        assert!(!tape.is_empty(), "{name} should contain an operation");
    }
}

#[test]
fn documented_fixture_command_passes() {
    let root = tool_root();
    let output_dir = env::temp_dir().join(format!("rustytwin-docs-{}", std::process::id()));
    let _ = fs::remove_dir_all(&output_dir);
    let output = Command::new(env!("CARGO_BIN_EXE_rustytwin"))
        .args([
            "check",
            "--baseline-bin",
            root.join("fixtures/equivalent/baseline.sh")
                .to_str()
                .unwrap(),
            "--candidate-bin",
            root.join("fixtures/equivalent/candidate.sh")
                .to_str()
                .unwrap(),
            "--tape",
            root.join("examples/simple_tape.ndjson").to_str().unwrap(),
            "--out",
            output_dir.to_str().unwrap(),
        ])
        .output()
        .unwrap();

    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stdout)
    );
    let _ = fs::remove_dir_all(output_dir);
}
