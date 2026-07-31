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

## Scope

RustyTwin currently compares process-observable behavior. It does not yet
build revisions automatically from source, inspect ABI, parse Clang ASTs,
generate or shrink operations, or run a full Mako Raft cluster. Planned work
includes named module cases, stateful operation generation, migration rules,
CI reports, and deterministic Raft-server coverage.
