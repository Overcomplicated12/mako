# RustyTwin Examples

These walkthroughs keep the repository README focused on the everyday module
workflow. They are longer examples for specific migration situations.

| Example | When to use it |
| --- | --- |
| [RRR transport identity check](rrr-transport.md) | Verify the configured RRR-backed Raft transport target and RustyTwin wiring. |
| [Raft quorum sabotage](raft-quorum-sabotage.md) | Prove that a known quorum regression produces a useful divergence artifact. |
| [Custom NDJSON harness](custom-ndjson.md) | Compare behavior that is not naturally represented by a focused GoogleTest. |

Operation tapes in this directory:

| Tape | Purpose |
| --- | --- |
| `simple_tape.ndjson` | Minimal generic-harness fixture operations. |
| `gtest_smoke.ndjson` | Fixture GoogleTest operations. |
| `raft_helpers_tape.ndjson` | Future Raft-helper adapter operations. |
| `raft_quorum_smoke.ndjson` | Raft quorum smoke operations. |

The documentation test suite parses every tape. Examples that refer to Mako
build directories are intentionally not executed in CI because they require a
locally configured Clang 22 build.
