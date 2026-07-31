# RustyTwin

RustyTwin compares a baseline implementation with a RustyCpp-migrated
candidate. It runs the same focused check on both sides, compares the observed
result, and saves a replay artifact when they diverge.

It is designed for small, deterministic migration boundaries: a focused
GoogleTest target, helper, value type, or codec. It does not replace Mako's
C++ build configuration; Mako targets continue to use the configured C++23,
Clang 22, and CMake toolchain.

## Quick Start

Run these commands from the Mako repository root.

Build and test RustyTwin:

```bash
cargo build --locked --manifest-path tools/rustytwin/Cargo.toml
cargo test --locked --manifest-path tools/rustytwin/Cargo.toml
```

Create a manifest for a focused GoogleTest target. This records the test name,
default filter, timeout, and the two build directories.

```bash
cargo run --locked --manifest-path tools/rustytwin/Cargo.toml -- init \
  --module tools/rustytwin/modules/raft-quorum.toml \
  --test-target test_raft_quorum \
  --baseline-build "$PWD/build22-baseline" \
  --candidate-build /tmp/mako-raft-sabotage/build22-candidate \
  --filter RaftQuorumTest.HelperPredicates \
  --timeout-ms 5000
```

Validate the environment and both configured targets:

```bash
cargo run --locked --manifest-path tools/rustytwin/Cargo.toml -- doctor \
  --module tools/rustytwin/modules/raft-quorum.toml
```

Build each target and compare them:

```bash
cargo run --locked --manifest-path tools/rustytwin/Cargo.toml -- check \
  tools/rustytwin/modules/raft-quorum.toml \
  --build \
  --out /tmp/rustytwin-raft-quorum \
  --show-output
```

`--build` runs `cmake --build <build-dir> --target <test-target>` before the
comparison. A failed build stops the command and prints the relevant CMake
diagnostics. When both sides use the same build directory, RustyTwin builds it
once and reports an identity smoke check.

## How It Works

RustyTwin is a process-level differential checker:

1. A module manifest identifies the adapter, CMake target, optional GoogleTest
   filter, timeout, and baseline/candidate build directories.
2. `check --build` asks CMake to build that target on each side. Without
   `--build`, RustyTwin uses the already-built executables.
3. The adapter launches each executable separately with the same operation.
   For `gtest`, that operation is a `--gtest_filter`; for a custom harness it
   is an NDJSON operation tape on standard input.
4. RustyTwin captures standard output, standard error, exit status, timeout,
   and structured events. It compares the two observable results in order.
5. A match exits `0`. A mismatch exits `1` and writes a JSON replay artifact
   containing both captures, operations, metadata, and the first divergence.

The code is intentionally split by responsibility:

| Component | Responsibility |
| --- | --- |
| `src/main.rs` | CLI parsing and command orchestration. |
| `src/module.rs` | TOML manifest loading, validation, and target discovery. |
| `src/runner.rs` | Child-process execution, timeouts, output capture, and gtest adaptation. |
| `src/protocol.rs` | Typed operation, event, and harness-result data. |
| `src/compare.rs` | Canonicalization and first-divergence comparison. |
| `src/replay.rs` and `src/report.rs` | Failure artifact persistence and terminal reports. |

## Everyday Commands

| Command | Use |
| --- | --- |
| `init` | Create a GoogleTest module manifest. |
| `doctor` | Check the manifest, Clang 22, CMake, build directories, and test targets. |
| `check --module` | Build optionally, run the two tests, and compare results. |
| `replay` | Read a saved divergence artifact. |

Override a manifest setting without editing it:

```bash
cargo run --locked --manifest-path tools/rustytwin/Cargo.toml -- check \
  tools/rustytwin/modules/raft-quorum.toml \
  --baseline-build "$PWD/build22-baseline" \
  --candidate-build /tmp/mako-raft-sabotage/build22-candidate \
  --filter RaftQuorumTest.HelperPredicates \
  --out /tmp/rustytwin-raft-quorum
```

A successful comparison exits `0`. A behavioral mismatch exits `1` and prints
the artifact path. Replay it with:

```bash
cargo run --locked --manifest-path tools/rustytwin/Cargo.toml -- replay \
  /tmp/rustytwin-raft-quorum/rustytwin-failure-0001.json
```

## Exit Codes

| Code | Meaning | Typical response |
| --- | --- | --- |
| `0` | The requested command completed successfully; a check matched or `doctor` found no blocking issue. | Continue with the migration or CI job. |
| `1` | A behavioral check diverged, or `doctor` found an invalid configured build or target. | Inspect the report or artifact, then fix the named side. |
| `2` | RustyTwin could not interpret the command, manifest, tape, executable, or build command. | Correct the input or environment error printed on standard error. |

## Failure Guide

| Symptom | What it means | First command to run |
| --- | --- | --- |
| Build failure | `--build` could not produce the configured CMake target. | `cmake --build <build-dir> --target <test-target>` |
| Timeout | One harness did not finish before `timeout_ms`. | Run the failing binary directly with its gtest filter, then increase the timeout only when the behavior is expected. |
| Exit-status mismatch | One side passed while the other failed or crashed. | Run both test binaries directly with the same `--gtest_filter`. |
| Event mismatch | Custom harnesses emitted different structured events. | `rustytwin replay <artifact>` and compare each harness's NDJSON output for the reported step. |

`--show-output` prints the captured diagnostics during a check. The replay
artifact preserves both captures even when that flag is omitted.

## Module Manifest

`init` writes ordinary TOML. The only adapter currently supported by manifests
is `gtest`.

```toml
[module]
adapter = "gtest"
test_target = "test_raft_quorum"
filter = "RaftQuorumTest.HelperPredicates"
timeout_ms = 5000

[baseline]
build_dir = "/absolute/path/to/build22-baseline"

[candidate]
build_dir = "/absolute/path/to/build22-candidate"
```

`test_target` is the CMake target and executable name below each build
directory. `filter` is optional; without it RustyTwin runs the full GoogleTest
binary once. `doctor` warns when the two build directories are the same: that
proves the RustyTwin wiring works, but not migration equivalence.

## GoogleTest Checks

The module workflow is the preferred route for repeatable Mako checks. For a
one-off focused test, use the direct adapter instead:

```bash
cargo run --locked --manifest-path tools/rustytwin/Cargo.toml -- gtest-check \
  --baseline-test /path/to/baseline/test_raft_quorum \
  --candidate-test /path/to/candidate/test_raft_quorum \
  --filter RaftQuorumTest.HelperPredicates \
  --out /tmp/rustytwin-raft-quorum
```

Use `--show-output` when you want the captured GoogleTest diagnostics in the
console. Omit both `--filter` and `--tape` to run the full test binary once.
Use `--tape` only for an ordered sequence of GoogleTest filters; it cannot be
combined with `--filter`.

## Custom Harnesses And NDJSON

For behavior that a focused GoogleTest cannot express, use the generic `check`
command with a small executable harness per implementation:

```bash
cargo run --locked --manifest-path tools/rustytwin/Cargo.toml -- check \
  --baseline-bin /path/to/baseline-harness \
  --candidate-bin /path/to/candidate-harness \
  --tape /path/to/module-operations.ndjson \
  --out /tmp/rustytwin-module-check \
  --timeout-ms 5000
```

RustyTwin writes one operation per line to both harnesses:

```json
{"kind":"operation","step":1,"op":"majority_count","args":{"replicas":5}}
```

Each harness writes one observable event per line to standard output:

```json
{"kind":"event","step":1,"event":"return","value":3}
```

Put logs and framework diagnostics on standard error. RustyTwin compares event
order and JSON structure, normalizes trailing output whitespace, and ignores
elapsed-time metadata. Avoid addresses, wall-clock values, random IDs, and
unordered collections in observable events.

## Examples

The short Raft check above is the primary README example. Longer walkthroughs
live in [examples/README.md](examples/README.md):

- [RRR transport identity check](examples/rrr-transport.md)
- [Raft quorum sabotage demonstration](examples/raft-quorum-sabotage.md)
- [Custom NDJSON harness](examples/custom-ndjson.md)

The existing `*.ndjson` files in that directory are ready-to-parse operation
tapes used by the fixtures and future module adapters.

## Glossary

| Term | Meaning |
| --- | --- |
| Baseline | The known-good implementation or build used as the reference. |
| Candidate | The migrated implementation or build being compared with the baseline. |
| Identity smoke check | Runs the same build on both sides to validate RustyTwin wiring; it is not migration evidence. |
| Equivalence check | Runs separate baseline and migrated builds to test observable behavioral agreement. |
| Adapter | The bridge from a test style to RustyTwin events; currently generic harness and GoogleTest adapters exist. |
| Harness | A small executable that receives operations and emits observable events for a custom check. |
| Tape | An NDJSON sequence of ordered operations passed to a custom harness or interpreted by an adapter. |
| Replay artifact | JSON evidence saved for a failed check, including inputs, captures, and the first divergence. |

## Bringing Up A Module

1. Start with a narrow, deterministic behavior boundary and a normal focused
   test that already passes on each build.
2. Begin with an identity smoke check if only one build exists. Record it as a
   wiring check, not migration evidence.
3. Build the baseline and candidate with the same compiler family, build
   options, and relevant runtime configuration.
4. Run `doctor`, then `check --build`; inspect the replay artifact before
   changing either implementation after a mismatch.
5. Add a regression test or operation whenever the comparison catches a
   meaningful behavior difference. Leave cluster timing, real network I/O,
   filesystem persistence, and broad lifecycle scenarios for a later,
   explicitly deterministic layer.

## Fixtures

The fixture suite is useful for trying the generic harness protocol without a
Mako build:

```bash
cargo run --locked --manifest-path tools/rustytwin/Cargo.toml -- check \
  --baseline-bin tools/rustytwin/fixtures/equivalent/baseline.sh \
  --candidate-bin tools/rustytwin/fixtures/equivalent/candidate.sh \
  --tape tools/rustytwin/examples/simple_tape.ndjson \
  --out /tmp/rustytwin-equivalent
```

`fixtures/return_mismatch`, `state_mismatch`, `crash_mismatch`, and
`timeout_mismatch` demonstrate the recorded failure modes. The fixtures need
Python 3; RustyTwin itself needs Rust and Cargo.

## Command Reference

`rustytwin --help` is the authoritative command reference. The block below is
kept in sync with the CLI by `tests/documentation.rs`.

```text
Usage:
  rustytwin init --module <path> --test-target <target> [--baseline-build <dir>] [--candidate-build <dir>] [--filter <gtest-filter>] [--timeout-ms <ms>]
  rustytwin doctor --module <path> [--baseline-build <dir>] [--candidate-build <dir>]
  rustytwin check --module <path> --out <dir> [--build] [--baseline-build <dir>] [--candidate-build <dir>] [--filter <gtest-filter>] [--timeout-ms <ms>] [--show-output]
  rustytwin check <module-path> --out <dir> [module check options]
  rustytwin check --baseline-bin <path> --candidate-bin <path> --tape <path> --out <dir> [--timeout-ms <ms>] [--show-output]
  rustytwin gtest-check --baseline-test <path> --candidate-test <path> --out <dir> [--filter <gtest-filter> | --tape <path>] [--timeout-ms <ms>] [--show-output]
  rustytwin replay <artifact>
```

## Scope

RustyTwin currently compares process-observable behavior. It does not yet
build revisions automatically from source, inspect ABI, parse Clang ASTs,
generate or shrink operations, or run a full Mako Raft cluster. Planned work
includes named module cases, stateful operation generation, migration rules,
CI reports, and deterministic Raft-server coverage.
