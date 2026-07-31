# RRR Transport Identity Check

Use this when you want a quick check that the RRR-backed Raft transport target
builds and runs through RustyTwin. It is an identity smoke check: the same
binary is used on both sides, so it validates tool wiring rather than migration
equivalence.

From the Mako repository root:

```bash
cmake --build build22-wrapper --target test_raft_rrr_transport_compile -j2
```

```bash
cargo run --locked --manifest-path tools/rustytwin/Cargo.toml -- gtest-check \
  --baseline-test "$PWD/build22-wrapper/test_raft_rrr_transport_compile" \
  --candidate-test "$PWD/build22-wrapper/test_raft_rrr_transport_compile" \
  --filter RaftRrrTransportCompileTest.FactorySymbolLinks \
  --out /tmp/rustytwin-rrr-check \
  --show-output
```

Expected result: `Behavioral migration check: PASSED` and exit code `0`.
For a real equivalence check, replace one side with a separately built migrated
RRR target that exercises the same filter.
