//! Persistent failure artifacts.
//!
//! Artifacts capture the comparison inputs and both observed results. Replay
//! currently renders that saved record; it intentionally does not re-run the
//! original binaries, which may no longer exist or behave deterministically.

use std::fs;
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

use crate::protocol::{
    CheckMetadata, Comparison, FailureReport, HarnessResult, Operation, ReplayArtifact,
    PROTOCOL_VERSION,
};
use crate::VERSION;

/// Save a self-contained artifact for a failed comparison.
///
/// Artifact names are allocated monotonically within `output_dir` so repeated
/// failures do not overwrite prior evidence.
pub fn save_failure(
    output_dir: &Path,
    metadata: CheckMetadata,
    operations: Vec<Operation>,
    baseline: HarnessResult,
    candidate: HarnessResult,
    comparison: Comparison,
) -> Result<PathBuf, String> {
    let divergence = comparison
        .divergence
        .clone()
        .ok_or_else(|| "cannot save a replay artifact for a passing comparison".to_owned())?;
    fs::create_dir_all(output_dir).map_err(|error| {
        format!(
            "could not create replay directory {}: {error}",
            output_dir.display()
        )
    })?;

    let artifact = ReplayArtifact {
        protocol_version: PROTOCOL_VERSION,
        rustytwin_version: VERSION.to_owned(),
        timestamp_unix_ms: timestamp_unix_ms(),
        metadata,
        operations,
        baseline,
        candidate,
        comparison,
        failure: FailureReport {
            title: "Behavioral migration check: FAILED".to_owned(),
            divergence,
        },
    };

    let path = next_artifact_path(output_dir);
    let json = serde_json::to_vec_pretty(&artifact)
        .map_err(|error| format!("could not encode replay artifact: {error}"))?;
    fs::write(&path, json).map_err(|error| {
        format!(
            "could not write replay artifact {}: {error}",
            path.display()
        )
    })?;
    Ok(path)
}

/// Load a previously saved replay artifact without executing either harness.
pub fn load_artifact(path: &Path) -> Result<ReplayArtifact, String> {
    let contents = fs::read(path)
        .map_err(|error| format!("could not read replay artifact {}: {error}", path.display()))?;
    serde_json::from_slice(&contents).map_err(|error| {
        format!(
            "could not parse replay artifact {}: {error}",
            path.display()
        )
    })
}

fn timestamp_unix_ms() -> u128 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis()
}

fn next_artifact_path(output_dir: &Path) -> PathBuf {
    for index in 1.. {
        let path = output_dir.join(format!("rustytwin-failure-{index:04}.json"));
        if !path.exists() {
            return path;
        }
    }
    unreachable!("u64 artifact index space is exhausted")
}

#[cfg(test)]
mod tests {
    use std::collections::BTreeMap;
    use std::env;

    use serde_json::json;

    use super::*;
    use crate::protocol::{Comparison, Divergence, DivergenceKind, ExitStatusInfo, HarnessEvent};

    fn result(value: bool) -> HarnessResult {
        let mut fields = BTreeMap::new();
        fields.insert("value".to_owned(), json!(value));
        HarnessResult {
            events: vec![HarnessEvent {
                kind: "event".to_owned(),
                step: 1,
                event: "return".to_owned(),
                fields,
            }],
            stdout: String::new(),
            stderr: String::new(),
            exit_status: ExitStatusInfo {
                success: true,
                code: Some(0),
                signal: None,
            },
            timed_out: false,
            parse_error: None,
        }
    }

    #[test]
    fn saved_artifact_round_trips() {
        let output_dir =
            env::temp_dir().join(format!("rustytwin-replay-test-{}", std::process::id()));
        let _ = fs::remove_dir_all(&output_dir);
        let comparison = Comparison {
            passed: false,
            divergence: Some(Divergence {
                kind: DivergenceKind::Event,
                message: "harness events differ".to_owned(),
                step: Some(1),
                baseline: Some(json!(true)),
                candidate: Some(json!(false)),
            }),
        };
        let path = save_failure(
            &output_dir,
            CheckMetadata {
                baseline_bin: "baseline".to_owned(),
                candidate_bin: "candidate".to_owned(),
                tape_path: "tape.ndjson".to_owned(),
                timeout_ms: 50,
            },
            vec![],
            result(true),
            result(false),
            comparison,
        )
        .unwrap();

        let artifact = load_artifact(&path).unwrap();
        assert_eq!(artifact.failure.divergence.step, Some(1));
        let _ = fs::remove_dir_all(output_dir);
    }
}
