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

## Raft Quorum Smoke Check

RustyTwin can wrap the focused Raft quorum test target as a small integration
smoke check. This uses real code from `src/deptran/raft/quorum.hpp`, including
the inline-Rust DSL-backed helper predicates.

First build and run the existing target from a configured Mako Clang 22 build
directory. Substitute your own build directory when it differs:

```bash
cmake --build build22-wrapper --target test_raft_quorum -j2
build22-wrapper/test_raft_quorum --gtest_color=no
```

Then point the checked-in NDJSON adapter at that executable. Using the same
adapter on both sides is an identity smoke check: it verifies RustyTwin's
process, timeout, output-capture, and comparison paths against the real Raft
test suite.

```bash
env RUSTYTWIN_RAFT_QUORUM_TEST="$PWD/build22-wrapper/test_raft_quorum" \
cargo run --locked --manifest-path tools/rustytwin/Cargo.toml -- check \
  --baseline-bin tools/rustytwin/adapters/raft_quorum_gtest_harness.py \
  --candidate-bin tools/rustytwin/adapters/raft_quorum_gtest_harness.py \
  --tape tools/rustytwin/examples/raft_quorum_smoke.ndjson \
  --out /tmp/rustytwin-raft-quorum \
  --timeout-ms 5000
```

For a migration-equivalence check, build separate pre-migration and migrated
Raft harnesses. Create one small executable wrapper per build that sets
`RUSTYTWIN_RAFT_QUORUM_TEST` to its own test binary, then invokes
`raft_quorum_gtest_harness.py`; pass those two wrapper paths as
`--baseline-bin` and `--candidate-bin`. The identity smoke check cannot
establish equivalence by itself.

## Module Test Process

Use this process when bringing any Mako module under RustyTwin. Keep the first
operation tape small and deterministic; grow it only after the basic boundary
is trustworthy.

1. Build and run the module's normal focused test target first. Use Mako's
   configured C++23 and Clang 22 toolchain, and fix ordinary test failures
   before involving RustyTwin.
2. Choose a narrow behavior boundary: a pure helper, value type, codec, or
   focused test target. Avoid cluster timing, real network I/O, filesystem
   persistence, and broad server lifecycle behavior in the first tape.
3. Define one NDJSON operation for each observable behavior. Give every
   operation a stable `step`, name, and JSON arguments. A harness should emit
   exactly one event for each operation, including the return value or a small
   state summary needed for comparison.
4. Write a thin executable harness per implementation. It reads NDJSON from
   standard input, calls the module boundary, and writes NDJSON only to
   standard output. Send logs and diagnostics to standard error. Keep harness
   policy out of the module itself.
5. Start with an identity smoke check when only one build exists. Run the same
   harness on both sides to validate process launching, input handling,
   timeout behavior, and event parsing. Record it as a smoke check, not as
   migration evidence.
6. For an equivalence check, build the baseline and DSL-migrated versions
   separately with the same compiler family, build options, and relevant
   configuration. Use two wrapper executables so each RustyTwin child process
   selects the correct binary without relying on shared environment state.
7. Run the differential check and preserve the output directory:

```bash
cargo run --locked --manifest-path tools/rustytwin/Cargo.toml -- check \
  --baseline-bin /absolute/path/to/baseline-harness \
  --candidate-bin /absolute/path/to/candidate-harness \
  --tape /absolute/path/to/module-operations.ndjson \
  --out /tmp/rustytwin-module-check \
  --timeout-ms 5000
```

8. Treat a nonzero exit as a failed comparison. Inspect and share the saved
   artifact before changing either implementation:

```bash
cargo run --locked --manifest-path tools/rustytwin/Cargo.toml -- replay \
  /tmp/rustytwin-module-check/rustytwin-failure-0001.json
```

9. Add regression coverage when the tape exposes a meaningful behavior. Keep
   fast identity smoke checks near the module adapter, and reserve broader
   integration scenarios for a later, explicitly deterministic test layer.

Current canonicalization ignores only elapsed-time-style metadata. Harnesses
should avoid emitting addresses, wall-clock values, random IDs, unordered
collections, or verbose framework output on standard output.

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
