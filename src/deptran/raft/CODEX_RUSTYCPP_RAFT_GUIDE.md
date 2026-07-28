# Codex Guide: Raft Inline-Rust DSL Migration

This file is for future Codex sessions working in `src/deptran/raft`.
Treat it as the local operating guide for migrating Mako's Raft code toward
the RustyCpp inline-Rust DSL.

## Core Rules

- The `#if RUSTYCPP_RUST` block is the source of truth.
- Never hand-edit generated fallback inside `RUSTYCPP:GEN-BEGIN` /
  `RUSTYCPP:GEN-END`.
- Never hand-edit `rust_sha256`, `id`, or `version` markers.
- A hash mismatch means the generated fallback is stale. Regenerate or revert
  the Rust source; do not patch the marker.
- Keep commits small and bisectable.
- Separate C++ reshape commits from DSL migration commits when the reshape is
  nontrivial.
- Do not stage local build config, build directories, inventory output, or
  unrelated regen churn.

## Current Raft Strategy

Phase 1 was low-risk helper and POD migration: enums, value structs, config
helpers, scalar predicates, quorum/count helpers, and small wrapper functions.

Phase 2 is trait hierarchy and adapter work. It is not general cleanup.
Start with small virtual interfaces, then adapt concrete implementors close
behind the base trait.

Phase 2 completed areas:

1. Snapshot stream interfaces:
   `SnapshotWriter` and `SnapshotReader` in `snapshot_manager.hpp`.
2. Snapshot stream implementations:
   `MemorySnapshotWriter`, `MemorySnapshotReader`, `FileSnapshotWriter`, and
   `FileSnapshotReader`.
3. Snapshot manager interface and adapters:
   `SnapshotManager`, `MemorySnapshotManager`, and `FileSnapshotManager`.
4. Log storage interface and adapters:
   `LogStorage`, `InMemoryLogStorage`, and `RocksDBLogStorage`.
5. Dispatcher interface and adapter:
   `DispatcherBase` and `DummyDispatcher`.
6. Transport interface and adapters:
   `TransportBase`, `ChannelTransportAdapter`, and `RrrTransportAdapter`.
7. Known nested/local POD aggregates:
   `RaftWorkerPendingLog`, `RaftServerPendingAppendEntries`, and
   `ReplicatedDBFileEntry`.

Still avoid pulling these into Phase 2:

- `RaftServer`: too much consensus, persistence, locking, and orchestration.
- `CoordinatorRaft`, `RaftCommo`, `RaftServiceImpl`, `RaftFrame`,
  `RaftExecutor`, `ReplicatedDB`, and `RecoveryManager`.

Remaining Phase 2-style work should be limited to newly discovered small
POD/value records or small adapter/test fixtures. Treat large orchestration
classes as Phase 3 work.

## Translation Pattern

Preserve call-site shape whenever possible:

- Keep methods as methods.
- Delegate gnarly bodies to C++ free functions.
- Keep locks, callbacks, RPC, filesystem, RocksDB, raw buffer writes, and
  consensus orchestration in C++ unless a small isolated probe proves the DSL
  can lower the exact pattern cleanly.
- Use inline-Rust for pure predicates, value construction, simple trait
  surfaces, POD/config shape, and thin methods.

When a method body needs raw pointers, try/catch, syscalls, file I/O, or complex
ownership, keep the body in an annotated C++ helper and have the DSL method call
that helper.

## Trait Work

For a virtual interface:

1. Inspect all call sites and implementors.
2. Decide which methods are trait methods and which bodies stay C++.
3. Probe risky signatures in isolation before editing hot code.
4. Migrate the base trait.
5. Adapt concrete implementors soon after, one small commit at a time.

Do not leave a broad half-migrated vtable shape for long. A migrated trait with
hand-written implementors can be acceptable as a small stepping stone, but the
next work should prove and adapt at least one concrete implementor.

Use hand-written C++ classes as trait hand-bridges when a concrete type cannot
be expressed cleanly in the DSL. A hand-bridge may still derive the DSL-emitted
trait and delegate to shared kernels.

## Regeneration And Verification

For direct file checks, use the transpiler commands explicitly:

```bash
third-party/rusty-cpp/target/release/rusty-cpp-transpiler inline-rust --check --files <files>
third-party/rusty-cpp/target/release/rusty-cpp-transpiler inline-rust --rewrite --files <files>
```

For the project post-pass workflow, use:

```bash
bash scripts/regen_storage_dsl.sh third-party/rusty-cpp/target/release/rusty-cpp-transpiler
```

Be careful: raw `--rewrite` can produce fallback formatting or expressions that
the repo wrapper normally normalizes. If a raw rewrite creates broad mechanical
churn, inspect it before committing. Keep only intentional source changes.

Normal Raft verification:

```bash
third-party/rusty-cpp/target/release/rusty-cpp-transpiler inline-rust --check --files \
  src/deptran/raft/server.h \
  src/deptran/raft/coordinator.h \
  src/deptran/raft/commo.h \
  src/deptran/raft/channel_transport.hpp \
  src/deptran/raft/snapshot_manager.hpp \
  src/deptran/raft/snapshot_format.hpp \
  src/deptran/raft/file_snapshot_manager.hpp \
  src/deptran/raft/memory_snapshot_manager.hpp \
  src/deptran/raft/recovery_manager.hpp \
  src/deptran/raft/memory_log_storage.hpp \
  src/deptran/raft/rocksdb_log_storage.hpp \
  src/deptran/raft/replicated_db.h \
  src/deptran/raft/quorum.hpp

git diff --check
ninja -C build -j4 txlog_core_obj
```

Also include `messages.hpp` and `log_storage.hpp` in broad Raft sweeps when
checking every inline-Rust file in the module.

## Probing

Before migrating a risky type or signature:

1. Copy the candidate DSL block into a scratch file in `/tmp`.
2. Run `inline-rust --rewrite`.
3. Read the emitted C++ for layout, inheritance, references, pointer types,
   constructors, and namespace behavior.
4. Only then edit the real source file.

Do this especially for:

- `pub trait` blocks.
- `#[cpp_inherit]` implementors.
- `#[cpp_ctor]` constructors.
- Raw pointers and out-parameters.
- Types using `std::vector` versus RustyCpp `Vec`.
- Any method returning references or owning pointers.

## Footguns

- `rusty::Rc<T>` by value is unsafe in current RustyCpp: its copy constructor is
  shallow and does not increment the refcount. Borrow with
  `const rusty::Rc<T>&`, move with `std::move`, or explicitly clone with
  `.clone()`.
- `Vec<T>` in the DSL means RustyCpp's port `rusty::Vec<T>`, not
  `std::vector<T>`. Spell `std::vector<u8>` when C++ vector semantics or
  interop are required.
- Use turbofish for container initializers when inference is weak.
- Avoid `.is_empty()` on mutex guards if the transpiler/library version does
  not support it; use `.len() == 0`.
- Enums emit as `enum class`; qualify every use.
- Do not expose DSL-migrated structs directly across `extern "C"` or generated
  wire boundaries.
- Treat raw pointer, `memcpy`, filesystem, RPC, RocksDB, and syscall kernels as
  C++ boundaries unless a focused probe proves otherwise.

## Reshape Before DSL

Use reshape commits to make C++ fit the DSL before migration:

- Collapse overloads into one signature when practical.
- Type-erase template callbacks with `rusty::Function` when useful.
- Hoist nested PODs to namespace scope.
- Lift anonymous enums to named enums and qualify all call sites.
- Convert internal raw out-pointers to references where behavior permits.
- Extract const methods that mutate state into `Cell`/`Mutex` patterns during
  DSL migration.
- Move static mutable state behind free functions or appropriate once-cell
  patterns.

If reshape would cause broad public churn, lose semantics, or unblock a whole
category only with transpiler support, stop and probe the current transpiler
before asking for a feature or declaring the type floor.

## Permanent Floor Is Small

Do not casually declare a file unmigratable. Classify the blocker by reason:

- True unsafe substrate: raw syscalls, raw byte kernels, `memcpy`, manual file
  descriptor behavior, and similar code the safe DSL is designed to sit above.
- Compile-time type metaprogramming: CRTP, SFINAE, type lists, variadic factory
  types.
- Third-party or generated wire types: convert at the edge, not across it.

Everything else should be treated as either convertible, not yet reshaped, or
blocked on one identifiable transpiler/library feature. Re-probe old floor
verdicts against the current transpiler.

## Before Committing

- Run `inline-rust --check` on touched files.
- Run the broader Raft check when the change could affect shared headers.
- Run `git diff --check`.
- Run `ninja -C build -j4 txlog_core_obj`.
- Inspect `git diff --name-only` and `git diff --stat`.
- Stage only relevant source/test files.
- If `.git` is read-only in the sandbox, request escalation only for staging
  and committing.

Good commit shapes:

- `raft: prep snapshot reader adapter for dsl`
- `raft: migrate memory snapshot reader to inline rust dsl`
- `raft: regenerate raft inline rust fallbacks`

Avoid mixing prep, migration, broad regen normalization, and unrelated cleanup
in one commit.
