use std::collections::BTreeMap;

use serde::{Deserialize, Serialize};
use serde_json::{Map, Value};

pub const PROTOCOL_VERSION: u32 = 1;

fn operation_kind() -> String {
    "operation".to_owned()
}

fn event_kind() -> String {
    "event".to_owned()
}

fn empty_object() -> Value {
    Value::Object(Map::new())
}

/// One operation delivered to both harnesses through their standard input.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct Operation {
    #[serde(default = "operation_kind")]
    pub kind: String,
    pub step: u64,
    pub op: String,
    #[serde(default = "empty_object")]
    pub args: Value,
}

/// One observable output event written by a harness as an NDJSON line.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct HarnessEvent {
    #[serde(default = "event_kind")]
    pub kind: String,
    pub step: u64,
    pub event: String,
    #[serde(flatten)]
    pub fields: BTreeMap<String, Value>,
}

/// Exit information that is stable enough to serialize in a replay artifact.
#[derive(Clone, Debug, Deserialize, PartialEq, Eq, Serialize)]
pub struct ExitStatusInfo {
    pub success: bool,
    pub code: Option<i32>,
    pub signal: Option<i32>,
}

/// Complete captured result from one harness execution.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct HarnessResult {
    pub events: Vec<HarnessEvent>,
    pub stdout: String,
    pub stderr: String,
    pub exit_status: ExitStatusInfo,
    pub timed_out: bool,
    pub parse_error: Option<String>,
}

/// The category of the first difference found by a behavioral check.
#[derive(Clone, Debug, Deserialize, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum DivergenceKind {
    Timeout,
    ExitStatus,
    OutputParse,
    EventCount,
    Event,
}

/// The first observable difference between baseline and candidate execution.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct Divergence {
    pub kind: DivergenceKind,
    pub message: String,
    pub step: Option<u64>,
    pub baseline: Option<Value>,
    pub candidate: Option<Value>,
}

/// Result of comparing the two harness executions.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct Comparison {
    pub passed: bool,
    pub divergence: Option<Divergence>,
}

/// User-facing failure details retained in the replay artifact.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct FailureReport {
    pub title: String,
    pub divergence: Divergence,
}

/// Inputs that describe one `rustytwin check` invocation.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct CheckMetadata {
    pub baseline_bin: String,
    pub candidate_bin: String,
    pub tape_path: String,
    pub timeout_ms: u64,
}

/// A self-contained record of a failed comparison. v0 replay displays this
/// record; a later version may also re-execute the harnesses from it.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct ReplayArtifact {
    pub protocol_version: u32,
    pub rustytwin_version: String,
    pub timestamp_unix_ms: u128,
    pub metadata: CheckMetadata,
    pub operations: Vec<Operation>,
    pub baseline: HarnessResult,
    pub candidate: HarnessResult,
    pub comparison: Comparison,
    pub failure: FailureReport,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn operation_defaults_to_an_empty_object() {
        let operation: Operation =
            serde_json::from_str(r#"{"step":1,"op":"majority_count"}"#).unwrap();

        assert_eq!(operation.kind, "operation");
        assert_eq!(operation.args, Value::Object(Map::new()));
    }

    #[test]
    fn event_keeps_payload_fields() {
        let event: HarnessEvent =
            serde_json::from_str(r#"{"kind":"event","step":3,"event":"return","value":true}"#)
                .unwrap();

        assert_eq!(event.fields.get("value"), Some(&Value::Bool(true)));
    }
}
