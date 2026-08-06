# Raft Phase 8.8 Clock Plan

## Goal

Make Raft's *monotonic time reads* injectable so the in-memory `TestCluster`
can advance time explicitly and exercise election-timeout behavior without
wall-clock sleeps. This phase is deliberately narrower than a scheduler
rewrite: production fibers still sleep through rrr, and production election
timeout randomization remains unchanged.

The result is a deterministic in-process API with this shape:

```cpp
cluster.advance_time_by_us(150'000);
cluster.step_election_timers();
```

No sockets, real-time waits, or test-only `bool test_mode` branches are
introduced.

## Existing Timing Surface

The time reads that must share one clock source are:

| Location | Current role | Phase 8.8 treatment |
| --- | --- | --- |
| `RaftServer::resetTimer` | Records last valid leader/vote contact | Replace `Time::now(false)` with `clock().now_us()` |
| `RaftServer::GetElectionTimeout` | Applies preferred-leader startup grace | Read startup age through `clock().now_us()` |
| `RaftServer::Setup` and `InitializeForInMemoryTest` | Capture startup timestamp | Capture the injected clock timestamp |
| `RaftServer::StartElectionTimer` | Checks elapsed election timeout | Keep its production fiber/sleep loop, but read elapsed time through the clock |
| Leadership-transfer monitor and transfer start | Measures stable-leader/transfer windows | Replace direct `Time::now(false)` reads |

`Fiber::sleep`, `usleep`, `rrr::Timer`, and `RandomGenerator` are explicitly
outside this phase. They are scheduling or policy dependencies, not clock
reads. Replacing them requires a separate `RaftScheduler`/timer-policy design.

## Contract

Create `src/deptran/raft/clock.hpp` with a move-only `RaftClockProxy` built
through `pro::facade_builder`, following `transport.hpp` and `dispatcher.hpp`.
The facade has exactly one operation:

```cpp
uint64_t now_us() const;
```

Rules:

- The epoch is opaque. Only elapsed-time subtraction and ordering are valid.
- Values are monotonic microseconds. A clock never moves backward.
- A server receives its clock through a named dependency and retains it for
  its entire lifetime.
- The clock does not own a server, transport, worker, poll thread, or timer.
- New code uses Rusty ownership/types. The system-clock adapter is the sole
  `@unsafe` boundary that calls `Time::now(false)`.

### Adapters

`SystemRaftClock` is the production adapter and returns `Time::now(false)`.
`make_system_raft_clock()` creates the normal production proxy.

`ManualRaftClock` is a shared test-owned clock. It stores microseconds in
`rusty::sync::atomic::AtomicU64` and exposes only:

```cpp
uint64_t now_us() const;
uint64_t advance_by_us(uint64_t delta_us);
```

Use `Acquire` loads and `AcqRel` fetch-adds because tests advance time on the
test thread while timer steps execute on node `PollThread`s. Construct it at
an explicit initial value (normally zero); do not expose a backwards-moving
`set_now` method. `make_manual_raft_clock(rusty::Arc<ManualRaftClock>)` wraps
the same shared instance in every test-server proxy.

## Injection And Lifetime

1. Add `rusty::Option<RaftClockProxy> clock_` to `RaftServer` and a checked
   private `clock()` accessor. Production construction installs
   `make_system_raft_clock()` before any startup-time read.
2. Add a required `RaftClockProxy clock` field to
   `RaftServerInMemoryTestDependencies`. This is a named dependency, not a
   test-mode flag.
3. Extend the in-memory dependency with an optional exact
   `election_timeout_us`. It exists only to make the single-step timer test
   deterministic; when absent, `GetElectionTimeout()` keeps today's preferred
   replica/grace/random behavior.
4. `TestCluster` owns one `rusty::Arc<ManualRaftClock>` for the entire cluster.
   `build()` and `restart()` give every replacement server a proxy to that same
   clock. The shared clock must be declared before `nodes_` so it outlives all
   servers, workers, and poll-thread jobs.
5. Restart preserves time. `reset_for_independent_test_section()` must either
   retain the clock intentionally or construct a new cluster; it must not
   silently move an existing manual clock backward.

## Server Changes

### 1. Centralize Time Reads

Replace RaftServer-owned `Time::now(false)` calls with `clock().now_us()`.
This includes startup timestamp initialization, `resetTimer`, election elapsed
checks, leadership-monitor elapsed checks, and leadership-transfer start.

Do not alter timestamps owned by unrelated `Frame`, `RaftCommo`, persistence,
or rrr code in this phase. Audit with:

```bash
rg -n 'Time::now\(false\)' src/deptran/raft/server.h src/deptran/raft/server.cc
```

The only allowed remaining RaftServer direct system-clock access after the
change is inside `SystemRaftClock`.

### 2. Add a Single-Step Election Timer

Extract the non-blocking decision from `StartElectionTimer()` into a private
helper conceptually equivalent to:

```cpp
bool CheckElectionTimeoutOnce(uint64_t now_us);
```

It calculates elapsed time from `last_heartbeat_time_`, obtains the active
timeout (using the explicit in-memory override when present), and starts one
election only when the existing `server_election_timeout_has_fired` predicate
permits it. It must retain all current term, `req_voting`, `stop_`, and mutex
rules.

`StartElectionTimer()` continues to own the production `Fiber::sleep` loop;
after each wake it calls this helper with `clock().now_us()`. The new public
test hook, `DriveElectionTimerOnceForInMemoryTest()`, schedules no fibers and
performs one helper invocation on the owning PollThread.

Do not call `RequestVote()` while holding `mtx_` if current code releases it
before the call. Preserve that sequencing exactly when extracting the helper.

### 3. TestCluster Controls

Add two facade-backed TestCluster operations:

```cpp
uint64_t advance_time_by_us(uint64_t delta_us);
bool step_election_timers();
```

`step_election_timers()` dispatches a single timer check to every live,
non-isolated node through `run_on_poll_thread()` and waits for all submitted
jobs. It must not invoke production `StartElectionTimer()` or create a timer
fiber. Each node gets a distinct fixed in-memory timeout (for example
150/250/350 microseconds) so a shared advance produces one intended candidate
rather than an artificial simultaneous-election storm.

## Tests

Add `tests/raft_clock_test.cc` and a `test_raft_clock` CMake target before
wiring RaftServer. Cover:

1. System clock returns a nonzero monotonic value across two reads.
2. Manual clock starts at its supplied epoch.
3. `advance_by_us` returns and publishes the new time.
4. Concurrent reader/advancer smoke coverage observes no backwards value.
5. Manual-clock proxy and system-clock proxy both satisfy the same facade.

Extend `tests/raft_test_cluster_test.cc` with deterministic real-server cases:

1. **No early election:** advance to one microsecond before site 1's timeout,
   step timers, and assert no leader / no term change.
2. **Exact timeout election:** advance the final microsecond, step timers,
   and assert exactly one elected leader with a term increase.
3. **Leader contact resets the deadline:** deliver an AppendEntries or
   empty-AppendEntries contact, advance less than a fresh timeout, and prove
   the follower does not start an election; advance past the new deadline and
   prove it does.
4. **Disconnected follower re-election:** isolate the leader/follower path,
   advance through the follower's fixed timeout, step timers, and verify a
   new majority-side leader without any wall-clock sleep.
5. **Restart keeps time monotonic:** advance, kill/restart one node, advance
   again, and prove the replacement observes the shared clock and its timeout
   from its new startup/reset point.

Keep the existing explicit `step_election()` and `step_replication()` tests;
they are still valuable transport and replication coverage.

## Implementation Order

1. Add the clock facade/adapters and independent unit tests.
2. Add `clock_` injection plus a production system-clock default; replace all
   RaftServer time reads in one reviewable commit.
3. Extract the single-step election decision without changing production fiber
   cadence; add a regression test that existing production timer setup still
   calls it.
4. Add shared manual-clock ownership and `advance_time_by_us` /
   `step_election_timers` to TestCluster.
5. Add the deterministic cluster tests, then run the complete Raft gate.
6. Update `docs/TODO-raft.md` to mark Phase 8.8 complete only after all gates
   pass. Do not fold in a scheduler abstraction, leadership-transfer policy
   changes, or unrelated testconf `usleep` cleanup.

## Verification Gate

Use the standard Clang 22 build directory:

```bash
cmake --build build --target \
  test_raft_clock test_raft_test_cluster test_raft_channel_transport \
  test_raft_quorum test_raft_storage_facade raft_lab_standalone -j8

ctest --test-dir build --output-on-failure -R \
  '^(test_raft_clock|test_raft_test_cluster|test_raft_channel_transport|test_raft_quorum|test_raft_storage_facade)$'

timeout 90s ./build/raft_lab_standalone
```

Then run the existing production-backed Raft lab and shard-replication gates
from `docs/TODO-raft.md`. Confirm that production time still comes from
`SystemRaftClock`, no test binary binds sockets, and no runtime RocksDB files
are created by the in-memory clock tests.

## Risks And Non-Goals

- A clock alone cannot wake sleeping production fibers. This plan intentionally
  adds a test-only *single-step* timer check instead of a fake sleep API.
- Random timeout policy is retained in production. Fixed in-memory timeout
  values are test bootstrap data only and must not leak into `Config`.
- `ManualRaftClock` is shared and atomic, but tests still wait for each
  PollThread job to finish before advancing/asserting; atomicity is not a
  substitute for deterministic test sequencing.
- Leadership-transfer monitor fibers continue to use real scheduling. Their
  timestamp reads become injectable, but deterministic transfer scheduling is
  a future scheduler phase.
- This phase should remain below roughly 500 lines of implementation plus
  tests. Split the single-step timer extraction into its own commit if needed.

## Commit Plan

1. `raft: phase 8.8a — add RaftClock and ManualRaftClock`
2. `raft: phase 8.8b — inject RaftClock into RaftServer timing`
3. `raft: phase 8.8c — step election time in TestCluster`
4. `raft: phase 8.8 — deterministic Raft timer coverage`
