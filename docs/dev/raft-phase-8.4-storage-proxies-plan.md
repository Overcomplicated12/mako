# Raft Phase 8.4 Storage Proxy Plan

## Boundary

`LogStorage` and `SnapshotManager` remain the polymorphic implementation
interfaces for RocksDB, file-backed, and in-memory backends. This phase removes
those virtual types from `RaftServer`'s stored boundary by placing a small,
owning facade in front of each one.

`LogStorageProxy` and `SnapshotManagerProxy` own the existing
`std::shared_ptr` implementation handle and mirror every interface method.
They make ownership, nullability, and the virtual-call boundary explicit while
preserving the existing backend implementations and their persistence behavior.

## Compatibility

`RaftServer::SetLogStorage`, `GetLogStorage`, `SetSnapshotManager`, and
`GetSnapshotManager` retain their `shared_ptr` API for production callers and
lab fixtures. The setters wrap their supplied implementation with the factory;
the getters expose the contained implementation for tests that temporarily
swap a backend.

The in-memory test bootstrap likewise receives its explicit shared ownership
inputs, then wraps them when installing server state.

## Verification

A focused facade test exercises every forwarding method against
`InMemoryLogStorage` and `MemorySnapshotManager`, including metadata, batches,
streaming snapshot reads/writes, pruning, and backend identity. Existing Raft
and snapshot tests remain the integration gate. The full gate is only complete
after the Raft lab tests 1-60 and all snapshot tests pass.
