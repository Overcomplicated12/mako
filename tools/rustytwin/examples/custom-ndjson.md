# Custom NDJSON Harness

Use a custom harness when a focused GoogleTest cannot expose the behavior you
need to compare. RustyTwin sends the same operation tape to both harnesses on
standard input and expects one JSON event per output line.

The included equivalent fixture is runnable without a Mako build:

```bash
cargo run --locked --manifest-path tools/rustytwin/Cargo.toml -- check \
  --baseline-bin tools/rustytwin/fixtures/equivalent/baseline.sh \
  --candidate-bin tools/rustytwin/fixtures/equivalent/candidate.sh \
  --tape tools/rustytwin/examples/simple_tape.ndjson \
  --out /tmp/rustytwin-equivalent
```

An operation tape line has a stable step, an adapter-owned operation name, and
JSON arguments:

```json
{"kind":"operation","step":1,"op":"majority_count","args":{"replicas":5}}
```

The harness emits its observable result as NDJSON on standard output:

```json
{"kind":"event","step":1,"event":"return","value":3}
```

Send logs to standard error. Keep observable events deterministic: omit memory
addresses, wall-clock values, random IDs, and unordered collections unless the
harness normalizes them first.
