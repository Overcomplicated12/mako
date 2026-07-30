# RustyTwin

RustyTwin is a small differential test runner for RustyCpp migrations. It
runs the same NDJSON operation tape through a baseline harness and a candidate
harness, then compares their ordered output events.

Version 0 deliberately proves the workflow with small process fixtures. It
does not yet build revisions, inspect ABI, parse Clang ASTs, generate
operations, shrink failures, or run a full Mako Raft cluster.

## Requirements

- Rust and Cargo. The repository currently validates this tool with Rust
  1.91.
- Python 3 for the included fixture harnesses.

RustyTwin does not compile C++ in v0. Future Mako adapters must be built using
Mako's configured C++23 and Clang 22.x CMake toolchain; this tool must not
invent a separate compiler configuration.

## Build And Test

From the repository root:

```bash
cargo build --manifest-path tools/rustytwin/Cargo.toml
cargo test --manifest-path tools/rustytwin/Cargo.toml
```

## Try The Fixtures

An equivalent pair passes even though each side reports a different elapsed
time. Elapsed metadata is intentionally ignored by v0 canonicalization.

```bash
cargo run --manifest-path tools/rustytwin/Cargo.toml -- check \
  --baseline-bin tools/rustytwin/fixtures/equivalent/baseline.sh \
  --candidate-bin tools/rustytwin/fixtures/equivalent/candidate.sh \
  --tape tools/rustytwin/examples/simple_tape.ndjson \
  --out /tmp/rustytwin-equivalent
```

The return-mismatch fixture exits with code `1` and saves a replay artifact:

```bash
cargo run --manifest-path tools/rustytwin/Cargo.toml -- check \
  --baseline-bin tools/rustytwin/fixtures/return_mismatch/baseline.sh \
  --candidate-bin tools/rustytwin/fixtures/return_mismatch/candidate.sh \
  --tape tools/rustytwin/examples/simple_tape.ndjson \
  --out /tmp/rustytwin-return-mismatch

cargo run --manifest-path tools/rustytwin/Cargo.toml -- replay \
  /tmp/rustytwin-return-mismatch/rustytwin-failure-0001.json
```

The `state_mismatch`, `crash_mismatch`, and `timeout_mismatch` fixture
directories exercise the remaining v0 failure paths. Pass a short
`--timeout-ms` value for the timeout fixture.

## Protocol

RustyTwin writes one operation per line to both harnesses:

```json
{"kind":"operation","step":1,"op":"majority_count","args":{"replicas":5}}
```

Each harness writes one observable event per line:

```json
{"kind":"event","step":1,"event":"return","value":3}
```

The MVP compares JSON structurally, preserves event order, ignores elapsed
time metadata, normalizes trailing standard-output whitespace, and treats a
candidate-only crash or timeout as a divergence.

## Files

- `src/protocol.rs`: typed NDJSON and replay-artifact structures.
- `src/runner.rs`: sequential process execution, timeouts, and output capture.
- `src/compare.rs`: canonical trace comparison.
- `src/replay.rs`: failure artifact persistence and loading.
- `src/report.rs`: terminal reports.
- `examples/raft_helpers_tape.ndjson`: a Mako-aware future adapter tape.

## Future Work

- Stateful operation generation and shrinking.
- A small real Mako Raft helper adapter.
- Full RaftServer and deterministic cluster coverage.
- ABI manifests and RustyCpp marker checks.
- Clang AST migration rules.
- HTML reports and CI integration.
