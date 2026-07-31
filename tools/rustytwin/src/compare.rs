//! Trace comparison and canonicalization.
//!
//! RustyTwin reports the earliest meaningful difference first. Process-level
//! failures take priority over event differences because an event trace is not
//! comparable when one side timed out, crashed, or emitted invalid NDJSON.

use serde_json::{to_value, Value};

use crate::protocol::{Comparison, Divergence, DivergenceKind, HarnessEvent, HarnessResult};

// Timing is diagnostic data rather than migration behavior. Keep this list
// intentionally small so the comparator does not hide real state changes.
const IGNORED_METADATA_FIELDS: &[&str] = &["elapsed", "elapsed_ms", "time", "timestamp"];

/// Compare two captured harness runs in failure-precedence order.
///
/// The comparison checks timeouts, process status, malformed output, event
/// count, and finally canonicalized event data. Only elapsed-time-style
/// metadata is ignored during canonicalization.
pub fn compare_results(baseline: &HarnessResult, candidate: &HarnessResult) -> Comparison {
    if baseline.timed_out != candidate.timed_out {
        return failed(
            DivergenceKind::Timeout,
            "only one harness timed out",
            None,
            json_value(&baseline.timed_out),
            json_value(&candidate.timed_out),
        );
    }

    if baseline.exit_status != candidate.exit_status {
        return failed(
            DivergenceKind::ExitStatus,
            "harness exit statuses differ",
            None,
            json_value(&baseline.exit_status),
            json_value(&candidate.exit_status),
        );
    }

    if baseline.parse_error != candidate.parse_error {
        return failed(
            DivergenceKind::OutputParse,
            "harness NDJSON parsing results differ",
            None,
            json_value(&baseline.parse_error),
            json_value(&candidate.parse_error),
        );
    }

    if baseline.events.len() != candidate.events.len() {
        return failed(
            DivergenceKind::EventCount,
            "harness event counts differ",
            None,
            json_value(&baseline.events.len()),
            json_value(&candidate.events.len()),
        );
    }

    for (baseline_event, candidate_event) in baseline.events.iter().zip(&candidate.events) {
        let baseline_value = canonical_event(baseline_event);
        let candidate_value = canonical_event(candidate_event);
        if baseline_value != candidate_value {
            return failed(
                DivergenceKind::Event,
                "harness events differ",
                Some(baseline_event.step.min(candidate_event.step)),
                Some(baseline_value),
                Some(candidate_value),
            );
        }
    }

    Comparison {
        passed: true,
        divergence: None,
    }
}

fn failed(
    kind: DivergenceKind,
    message: impl Into<String>,
    step: Option<u64>,
    baseline: Option<Value>,
    candidate: Option<Value>,
) -> Comparison {
    Comparison {
        passed: false,
        divergence: Some(Divergence {
            kind,
            message: message.into(),
            step,
            baseline,
            candidate,
        }),
    }
}

fn json_value<T: serde::Serialize>(value: &T) -> Option<Value> {
    to_value(value).ok()
}

fn canonical_event(event: &HarnessEvent) -> Value {
    let mut value = to_value(event).expect("HarnessEvent is serializable");
    strip_ignored_metadata(&mut value);
    value
}

fn strip_ignored_metadata(value: &mut Value) {
    match value {
        Value::Array(values) => {
            for value in values {
                strip_ignored_metadata(value);
            }
        }
        Value::Object(values) => {
            values.retain(|key, _| !IGNORED_METADATA_FIELDS.contains(&key.as_str()));
            for value in values.values_mut() {
                strip_ignored_metadata(value);
            }
        }
        _ => {}
    }
}

#[cfg(test)]
mod tests {
    use std::collections::BTreeMap;

    use serde_json::json;

    use super::*;
    use crate::protocol::{ExitStatusInfo, HarnessEvent};

    fn result(events: Vec<HarnessEvent>) -> HarnessResult {
        HarnessResult {
            events,
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

    fn event(step: u64, value: Value) -> HarnessEvent {
        let mut fields = BTreeMap::new();
        fields.insert("value".to_owned(), value);
        HarnessEvent {
            kind: "event".to_owned(),
            step,
            event: "return".to_owned(),
            fields,
        }
    }

    #[test]
    fn identical_traces_pass() {
        let baseline = result(vec![event(1, json!(3))]);
        let candidate = result(vec![event(1, json!(3))]);

        assert!(compare_results(&baseline, &candidate).passed);
    }

    #[test]
    fn metadata_does_not_change_an_event() {
        let baseline = result(vec![event(1, json!({"value": 3, "elapsed_ms": 1}))]);
        let candidate = result(vec![event(1, json!({"value": 3, "elapsed_ms": 99}))]);

        assert!(compare_results(&baseline, &candidate).passed);
    }

    #[test]
    fn reports_the_first_event_difference() {
        let baseline = result(vec![event(1, json!(3)), event(2, json!(true))]);
        let candidate = result(vec![event(1, json!(3)), event(2, json!(false))]);

        let comparison = compare_results(&baseline, &candidate);
        let divergence = comparison.divergence.unwrap();
        assert_eq!(divergence.kind, DivergenceKind::Event);
        assert_eq!(divergence.step, Some(2));
    }
}
