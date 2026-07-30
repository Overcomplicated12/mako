use serde_json::to_string_pretty;

use crate::protocol::{Comparison, ReplayArtifact};

pub fn render_comparison(comparison: &Comparison) -> String {
    if comparison.passed {
        return "Behavioral migration check: PASSED".to_owned();
    }

    let divergence = comparison
        .divergence
        .as_ref()
        .expect("failed comparisons have a divergence");
    let mut report = String::from("Behavioral migration check: FAILED\n\n");
    if let Some(step) = divergence.step {
        report.push_str(&format!("First divergence at step {step}\n\n"));
    } else {
        report.push_str("First divergence before event comparison\n\n");
    }
    report.push_str(&format!("Reason: {}\n", divergence.message));
    append_value(&mut report, "Baseline", divergence.baseline.as_ref());
    append_value(&mut report, "Candidate", divergence.candidate.as_ref());
    report.trim_end().to_owned()
}

pub fn render_artifact(artifact: &ReplayArtifact) -> String {
    let mut report = render_comparison(&artifact.comparison);
    report.push_str(&format!(
        "\n\nReplay artifact version: {}\nOperations: {}\nBaseline: {}\nCandidate: {}",
        artifact.rustytwin_version,
        artifact.operations.len(),
        artifact.metadata.baseline_bin,
        artifact.metadata.candidate_bin,
    ));
    report
}

fn append_value(report: &mut String, label: &str, value: Option<&serde_json::Value>) {
    if let Some(value) = value {
        let rendered = to_string_pretty(value).unwrap_or_else(|_| value.to_string());
        report.push_str(&format!(
            "\n\n{label}:\n  {}",
            rendered.replace('\n', "\n  ")
        ));
    }
}

#[cfg(test)]
mod tests {
    use serde_json::json;

    use super::*;
    use crate::protocol::{Comparison, Divergence, DivergenceKind};

    #[test]
    fn report_identifies_the_first_step() {
        let report = render_comparison(&Comparison {
            passed: false,
            divergence: Some(Divergence {
                kind: DivergenceKind::Event,
                message: "harness events differ".to_owned(),
                step: Some(3),
                baseline: Some(json!(true)),
                candidate: Some(json!(false)),
            }),
        });

        assert!(report.contains("step 3"));
        assert!(report.contains("Baseline"));
    }
}
