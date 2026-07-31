# Raft Quorum Sabotage Demonstration

This example proves that RustyTwin catches a small semantic regression. Keep
the broken source in a disposable worktree, never in the main Mako checkout.

The known regression changes the equality case in
`src/deptran/raft/quorum.hpp`:

```cpp
return received >= needed;
```

to:

```cpp
return received > needed;
```

That makes `raft_quorum_reached(3, 3)` false, so
`RaftQuorumTest.HelperPredicates` fails. With a good baseline at
`build22-baseline` and the broken worktree at `/tmp/mako-raft-sabotage`, create
the temporary module manifest:

```bash
cargo run --locked --manifest-path tools/rustytwin/Cargo.toml -- init \
  --module /tmp/rustytwin-raft-quorum.toml \
  --test-target test_raft_quorum \
  --baseline-build "$PWD/build22-baseline" \
  --candidate-build /tmp/mako-raft-sabotage/build22-candidate \
  --filter RaftQuorumTest.HelperPredicates \
  --timeout-ms 5000
```

Then build and compare:

```bash
cargo run --locked --manifest-path tools/rustytwin/Cargo.toml -- check \
  /tmp/rustytwin-raft-quorum.toml \
  --build \
  --out /tmp/rustytwin-raft-sabotage \
  --show-output
```

Expected result: exit code `1`, an exit-status mismatch, and
`/tmp/rustytwin-raft-sabotage/rustytwin-failure-0001.json`. Restore `>=`, rebuild
the candidate, and rerun the same command to make the check pass.
