# Raft Phase 8.5 TestCluster Plan

## Scope

Make `TestCluster` exercise real `RaftServer` instances without importing the
production `Setup()` contract. The in-memory harness supplies every dependency:
identity, complete membership, channel transport, in-memory log storage, and
memory snapshots.

## Bootstrap Contract

`RaftServerInMemoryTestDependencies` is the named bootstrap input. Its
constructor path does not read `Config`, require a `Frame`, initialize
ReplicatedDB, or start production timer fibers. It registers a no-op apply
callback and starts only the joinable apply worker needed to consume committed
in-memory entries.

Election and replication are explicitly stepped by the test harness. This is
deliberate: `HeartbeatLoop()` assumes a production `RaftCommo` and its timer
fibers require a running reactor. The deterministic in-memory replication
round still uses the real `RaftServer::OnAppendEntries` handlers through
`ChannelTransportAdapter` and `RaftServerDispatcher`.

## Ownership And Restart

Each `RaftNode` owns exactly one `RaftServer`; its worker owns the corresponding
move-only dispatcher. Killing a node first unregisters its channel endpoint,
which closes the worker receiver, then joins and destroys the worker. Only on
restart is the old server released, after no worker can retain its dispatcher.
A replacement server reuses the node's original in-memory log and snapshot
objects and is installed in a fresh worker with a fresh receiver.

Restart only clears directed disconnect faults involving the restarted site.
Unrelated directed drops and active partitions remain intact.

## Verification

The focused tests cover a converged election, agreement/commit propagation,
and a disconnected follower catching up only after `reset_faults()`. A lifecycle
test confirms restart retains unrelated faults. The gate runs
`test_raft_test_cluster` and `raft_lab_standalone` under the Clang 22 build.
