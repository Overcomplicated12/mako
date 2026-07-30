# RustyTwin MVP Plan

## Goal

RustyTwin is a differential testing tool for RustyCpp migrations. It compares
a baseline C++ revision and a migrated RustyCpp/inline-DSL revision by running
the same operation sequence against both and comparing canonical output traces.

RustyTwin is not intended to test the entire distributed system in its first
version. The MVP focuses on small migrated Raft helper and value logic, not
`RaftServer`, networking, RocksDB, or cluster scheduling.

## Context

- Repository: Mako.
- Initial migration area: `src/deptran/raft`.
- Recent work migrated low-risk Raft structs and helpers into inline Rust DSL
  with generated C++ fallbacks.
- RustyTwin will help future migrations by checking behavioral equivalence
  between a baseline and a candidate implementation.
- The first version must be useful, buildable, and demoable quickly.

## Constraints

- Do not make broad changes to existing Mako runtime code.
- Keep the tool self-contained under `tools/rustytwin/` wherever possible.
- Do not use Docker.
- Do not implement full Raft cluster testing yet.
- Do not implement full ABI checking yet.
- Do not implement Clang AST automation yet.
- Do not implement HTML reporting yet.
- Focus on a clean CLI, process runner, NDJSON protocol, comparison logic,
  replay artifacts, and small fixtures.
- Ask before modifying build files outside `tools/rustytwin/` unless that is
  absolutely necessary.

## Target MVP

RustyTwin v0 must be able to:

1. Launch baseline and candidate harness executables.
2. Send both the same NDJSON operation tape on standard input.
3. Capture standard output, standard error, exit status, and timeouts.
4. Parse each harness's NDJSON output trace.
5. Canonicalize simple traces.
6. Compare baseline and candidate results.
7. Report the first divergence clearly.
8. Save a replay artifact on divergence.
9. Replay a saved failure.
10. Include fixtures proving equivalent behavior, return mismatches,
    state/event mismatches, and candidate crashes or timeouts.

## Layout

```text
tools/rustytwin/
|- Cargo.toml
|- README.md
|- src/
|  |- main.rs
|  |- protocol.rs
|  |- runner.rs
|  |- compare.rs
|  |- replay.rs
|  `- report.rs
|- specs/
|  `- raft_helpers.yaml
|- fixtures/
|  |- equivalent/
|  |- return_mismatch/
|  |- state_mismatch/
|  `- crash_mismatch/
`- examples/
   `- simple_tape.ndjson

tests/rustytwin/
|- raft_helpers_adapter.cc
|- raft_observer.cc
`- raft_scenarios.cc
```

The `tests/rustytwin/` adapter is optional for v0. Do not add it or modify the
Mako build unless the standalone tool and fixtures are complete and the change
can remain small.

## CLI

```bash
rustytwin check \
  --baseline-bin <path-to-baseline-harness> \
  --candidate-bin <path-to-candidate-harness> \
  --tape tools/rustytwin/examples/simple_tape.ndjson \
  --out failures/

rustytwin replay failures/<failure>.json
```

`check` is required for v0. `replay` prints the stored result; rerunning the
harnesses from an artifact is a later enhancement.

The following command is explicitly future work and not part of v0:

```bash
rustytwin fuzz --spec tools/rustytwin/specs/raft_helpers.yaml \
  --seed 42 --sequences 1000
```

## NDJSON Protocol

Each operation sent to both harnesses has this shape:

```json
{"kind":"operation","step":1,"op":"majority_count","args":{"replicas":5}}
{"kind":"operation","step":2,"op":"is_memory_ack","args":{"ack_type":"memory"}}
{"kind":"operation","step":3,"op":"snapshot_supports_compression","args":{"compression":"none"}}
```

Each harness emits events such as:

```json
{"kind":"event","step":1,"event":"return","value":3}
{"kind":"event","step":2,"event":"return","value":true}
{"kind":"event","step":3,"event":"return","value":true}
```

For v0, canonicalization will:

- Compare JSON object values structurally.
- Preserve event order.
- Ignore optional metadata such as elapsed time.
- Normalize trailing whitespace on standard output.
- Treat a crash or timeout on only one side as a failure.

Do not over-engineer canonicalization in the MVP.

## Implementation Phases

### Phase 1: Repository Inspection

- Inspect repository structure.
- Confirm that a standalone Rust crate under `tools/rustytwin/` is
  straightforward.
- Check existing tool conventions.
- Do not modify files during this phase.
- Report the minimal planned files.

### Phase 2: Standalone Rust CLI Skeleton

- Create `tools/rustytwin/Cargo.toml`.
- Implement a minimal CLI using `clap` or a small manual parser.
- Add `check` and `replay` commands.
- Add `protocol.rs`, `runner.rs`, `compare.rs`, `replay.rs`, and `report.rs`.
- Verify `cargo build` inside `tools/rustytwin`.

### Phase 3: Protocol Types

Use `serde` and `serde_json` for NDJSON parsing and writing. Define typed Rust
structures for:

- `Operation`
- `HarnessEvent`
- `HarnessResult`
- `FailureReport`
- `ReplayArtifact`

### Phase 4: Process Runner

Implement runner behavior that:

- Starts baseline and candidate harnesses.
- Sends both the same operation tape through standard input.
- Captures standard output and error.
- Enforces a per-harness timeout.
- Records exit status or signal.
- Parses standard output as NDJSON events.
- Returns structured `HarnessResult` values.

For v0, running baseline then candidate is acceptable as long as both receive
identical input. Concurrent execution is future work.

### Phase 5: Trace Comparison

Implement comparison behavior that:

- Fails if only one side crashes.
- Fails if exit statuses differ.
- Fails if event counts differ.
- Compares events step by step.
- Reports the first differing step or event.
- Prints a clear terminal report.

Example output:

```text
Behavioral migration check: FAILED

First divergence at step 3

Baseline:
  {"kind":"event","step":3,"event":"return","value":true}

Candidate:
  {"kind":"event","step":3,"event":"return","value":false}

Replay artifact:
  failures/rustytwin-failure-0001.json
```

### Phase 6: Replay Artifacts

On failure, save a self-contained JSON artifact containing:

- Command-line metadata.
- Baseline and candidate binary paths.
- The operation tape.
- Baseline and candidate results.
- The first divergence.
- Timestamp.
- RustyTwin version when available.

Implement `rustytwin replay failures/<file>.json`. It prints the stored
comparison result in v0. The artifact layout must leave room for rerunning the
harnesses later.

### Phase 7: Fixtures

Create simple harness fixtures under `tools/rustytwin/fixtures/`. Python,
shell, or Rust fixtures are acceptable; choose the simplest approach.

Required fixtures:

1. `equivalent`: baseline and candidate produce identical NDJSON events, so
   `rustytwin check` passes.
2. `return_mismatch`: candidate returns a different value, so the check fails
   and identifies the first divergent step.
3. `state_mismatch`: candidate emits a different event sequence, so the check
   fails.
4. `crash_mismatch`: candidate exits nonzero or times out, so the check fails
   clearly.

Document exact fixture commands in `tools/rustytwin/README.md`.

### Phase 8: Mako-Aware Sample Tape

Add `tools/rustytwin/examples/raft_helpers_tape.ndjson` with operations that
represent migrated helper logic, including:

- Majority-count calculations.
- Ack-type classification.
- Snapshot-format checks.
- Command-payload presence.
- Command-kind classification.
- Recovery-cleanup predicates.

For v0, the Mako adapter may be stubbed or fixture-based. Proving the runner
and comparison workflow has priority over wiring real Mako helper calls.

### Phase 9: Optional Mako Adapter Skeleton

Only after the standalone tool and fixtures work, and only if it requires no
major build-system change, consider `tests/rustytwin/raft_helpers_adapter.cc`.
It would read NDJSON operations, dispatch a small set to migrated helpers, and
write NDJSON events.

Good first adapter targets:

- Pure helper functions.
- Enum predicates.
- Config or result helper wrappers.
- Message and value helpers.

Avoid:

- `RaftServer`
- `RaftWorker`
- `RaftCommo`
- RocksDB
- RPC
- Threads and mutexes
- Raw buffers
- Filesystem behavior

### Phase 10: README And Final Report

Write `tools/rustytwin/README.md` covering:

- RustyTwin's purpose for RustyCpp migration.
- The input and output protocol.
- Exact fixture commands.
- v0 capabilities.
- Future work.

Future work includes stateful sequence generation, shrinking, a full Mako Raft
adapter, ABI manifest comparison, structural RustyCpp marker checks, AST-based
migration rule checking, HTML reports, and CI integration.

## Acceptance Criteria

- `cargo build` succeeds in `tools/rustytwin`.
- `rustytwin check` passes for the equivalent fixture.
- `rustytwin check` fails for the return-mismatch fixture and identifies the
  first divergence.
- `rustytwin check` fails for the crash-mismatch fixture and explains the
  exit-status difference.
- Failures save replay artifacts.
- `rustytwin replay <artifact>` prints a useful stored report.
- The README has exact demo commands.
- No existing Mako runtime behavior changes.

## Approval Gate

Before implementation, report a concise Phase 1 plan covering:

1. Files to create.
2. Dependencies to add.
3. Whether any build-system changes are needed.
4. Exact first commands to run.
5. Risks and open questions.

After approval, implement Phases 1 through 7 only. Do not begin full Mako
adapter integration until the standalone tool and fixtures work.
