#pragma once

/**
 * @file test_cluster.hpp
 * @brief In-process RaftServer cluster harness. Wires N real RaftNodes
 *        together through a ChannelSwitchboard and exposes the fault-
 *        injection controls the lab tests need (kill / restart /
 *        disconnect / partition).
 *
 * The harness has an explicit, reduced startup contract: it supplies
 * identity, membership, channel transport, and in-memory storage to every
 * RaftServer without using production Setup(), Config, Frame, or ReplicatedDB.
 * Elections and replication are stepped deterministically by tests on the
 * owning per-node PollThread; each RPC still crosses the real channel
 * transport and RaftServerDispatcher boundary.
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include <rusty/arc.hpp>
#include <rusty/box.hpp>
#include <rusty/option.hpp>

#include "channel_transport.hpp"
#include "test_cluster_facade.hpp"
#include "../classic/tpc_command.h"
#include "memory_log_storage.hpp"
#include "memory_snapshot_manager.hpp"
#include "raft_node.hpp"

#include "../constants.h"
#include "rrr/rrr.hpp"

namespace janus {
namespace raft {

class TestCluster : public TestClusterFacade {
 public:
  // @safe - builds an N-site cluster wired through an internal
  // ChannelSwitchboard. Each site gets its own InMemoryLogStorage +
  // MemorySnapshotManager. Site IDs are 1..N.
  // @unsafe { direct `new` because Box::make() requires a copy }
  static rusty::Box<TestCluster> with_in_memory_transport(size_t n) {
    rusty::Box<TestCluster> c(new TestCluster());
    c->build(n);
    return c;
  }

  // @safe - accessors
  size_t size() const override { return nodes_.size(); }
  RaftNode& node(siteid_t id) {
    for (auto& n : nodes_) {
      if (n->id() == id) return *n;
    }
    // @unsafe { bogus id — abort via out-of-range dereference }
    return *nodes_.at(nodes_.size());  // throws std::out_of_range
  }
  ChannelSwitchboard& switchboard() { return sw_; }

  // @safe - returns the full site-id list.
  const std::vector<siteid_t>& site_ids() const override { return site_ids_; }
  RaftServer* node_server(siteid_t site) override {
    const size_t i = index_of(site);
    return i == nodes_.size() ? nullptr : nodes_[i]->server();
  }
  bool node_is_leader(siteid_t site) const override {
    const size_t i = index_of(site);
    return i != nodes_.size() && !dead_[i] && nodes_[i]->is_leader();
  }
  uint64_t node_current_term(siteid_t site) const override {
    const size_t i = index_of(site);
    return i == nodes_.size() || dead_[i] ? 0 : nodes_[i]->current_term();
  }
  uint64_t node_commit_index(siteid_t site) const override {
    const size_t i = index_of(site);
    return i == nodes_.size() || dead_[i] ? 0 : nodes_[i]->commit_index();
  }
  bool node_has_committed_command(siteid_t site, uint64_t index,
                                  int command_id) override {
    const size_t i = index_of(site);
    return i != nodes_.size() && !dead_[i] &&
           nodes_[i]->server()->HasCommittedCommandForInMemoryTest(
               index, command_id);
  }
  uint64_t node_rpc_count(siteid_t site) const override {
    return sw_.rpc_count(site);
  }

  // @safe - lifecycle inspection for the in-memory reactor owned by a node.
  bool has_live_poll_thread(siteid_t s) const {
    return poll_threads_[index_of(s)].is_some();
  }
  size_t poll_thread_generation(siteid_t s) const {
    return poll_thread_generations_[index_of(s)];
  }
  size_t poll_thread_join_count() const { return poll_thread_join_count_; }

  // ------------------------------------------------------------------
  // Fault injection
  // ------------------------------------------------------------------

  // @safe - stops all traffic from `s` to every peer and vice versa.
  void disconnect(siteid_t s) override {
    const size_t i = index_of(s);
    if (i != nodes_.size()) isolated_[i] = true;
    sw_.isolate_site(s);
  }

  // @safe - restores only traffic involving `s`; unrelated directed drops and
  // active partitions remain installed on the switchboard.
  void reconnect(siteid_t s) override {
    const size_t i = index_of(s);
    if (i != nodes_.size()) isolated_[i] = false;
    sw_.unisolate_site(s);
  }

  // @safe - splits sites into two groups that cannot exchange
  // messages across the boundary. Sites outside both groups remain
  // fully connected (via current switchboard semantics).
  void partition(std::vector<siteid_t> a, std::vector<siteid_t> b) override {
    sw_.partition({std::move(a), std::move(b)});
  }

  // @safe - clear all fault injections.
  void reset_faults() override {
    sw_.reset_faults();
    std::fill(isolated_.begin(), isolated_.end(), false);
  }

  // @safe - make a clean real-server cluster for an independent in-memory
  // lab section. Workers and poll threads are stopped before server teardown;
  // retained test storage is cleared only after no server can access it.
  void reset_for_independent_test_section() override {
    // Fence every queued delivery before taking down any one receiver. A
    // synchronous sender whose destination disappears must observe its reply
    // channel close; otherwise a peer PollThread can remain in an RPC wait
    // while this reset tries to join it.
    for (auto site : site_ids_) disconnect(site);
    for (auto site : site_ids_) kill(site);
    for (auto& log : logs_) (void)log->clear();
    for (auto& snapshot : snaps_) (void)snapshot->DeleteAllSnapshots();
    for (auto site : site_ids_) restart(site);
    reset_faults();
  }

  // @safe - starts an election on the first live node's PollThread. One
  // explicit reactor job keeps elections deterministic without enabling the
  // production timer fiber, which requires RaftCommo/Frame state.
  bool step_election(siteid_t candidate = 0) override {
    for (size_t i = 0; i < nodes_.size(); ++i) {
      if (dead_[i] || isolated_[i]) continue;
      if (candidate != 0 && nodes_[i]->id() != candidate) continue;
      auto result = std::make_shared<std::atomic<bool>>(false);
      if (!run_on_poll_thread(i, [server = nodes_[i]->server(), result]() {
            result->store(server->StartElectionForInMemoryTest(),
                          std::memory_order_release);
          })) {
        return false;
      }
      return result->load(std::memory_order_acquire);
    }
    return false;
  }

  // @safe - count the live servers currently reporting leadership.
  size_t leader_count() const {
    size_t leaders = 0;
    for (size_t i = 0; i < nodes_.size(); ++i) {
      if (!dead_[i] && nodes_[i]->is_leader()) ++leaders;
    }
    return leaders;
  }

  // @safe - append a well-formed classic commit through a specific real
  // server. It returns false when that node is dead or not the leader.
  bool append_command(siteid_t site, int command_id, uint64_t* index,
                      uint64_t* term = nullptr) override {
    const size_t i = index_of(site);
    if (i == nodes_.size() || dead_[i]) return false;
    auto command = rusty::Arc<TpcCommitCommand>::make();
    auto pieces = rusty::Arc<VecPieceData>::make();
    pieces.get_mut().unwrap().sp_vec_piece_data_ =
        std::make_shared<vector<shared_ptr<SimpleCommand>>>();
    {
      auto& mutable_command = command.get_mut().unwrap();
      mutable_command.tx_id_ = command_id;
      mutable_command.cmd_ = std::move(pieces);
    }
    auto envelope = std::make_shared<janus::Command>(
        janus::Command::pack_aliased<TpcCommitCommand>(std::move(command)));
    struct AppendResult {
      bool started = false;
      uint64_t index = 0;
      uint64_t term = 0;
    };
    auto result = std::make_shared<AppendResult>();
    if (!run_on_poll_thread(i, [server = nodes_[i]->server(), envelope,
                                 result]() {
          result->started = server->Start(*envelope, &result->index,
                                          &result->term);
        })) {
      return false;
    }
    if (!result->started) return false;
    *index = result->index;
    if (term != nullptr) *term = result->term;
    return true;
  }

  // @safe - append the smallest well-formed classic commit through the elected
  // real server. The apply path expects a TpcCommitCommand, even when the
  // test does not attach application work to it.
  bool append_noop_to_leader(uint64_t* index) {
    for (size_t i = 0; i < nodes_.size(); ++i) {
      if (dead_[i] || !nodes_[i]->is_leader()) continue;
      return append_command(nodes_[i]->id(), /*command_id=*/0, index);
    }
    return false;
  }

  // @safe - drive one heartbeat/append round on every live node's PollThread.
  // Only the leader sends traffic; other calls are no-ops.
  bool step_replication() override {
    for (size_t i = 0; i < nodes_.size(); ++i) {
      if (dead_[i] || isolated_[i]) continue;
      if (!run_on_poll_thread(i, [server = nodes_[i]->server()]() {
            (void)server->DriveReplicationOnceForInMemoryTest();
          })) {
        return false;
      }
    }
    return true;
  }

  // @safe - close and join the worker before its dispatcher can outlive the
  // server. The server itself is released during restart, after this ordering.
  void kill(siteid_t s) override {
    const size_t i = index_of(s);
    if (dead_[i]) return;
    disconnect(s);
    stop_worker(i);
    verify(run_on_poll_thread(i, [server = node(s).server()]() {
      server->setIsLeader(false);
    }));
    stop_poll_thread(i);
    // No worker or reactor job can now retain the server, so Kill has the
    // same destroy-now contract as the production lab harness.
    node(s).release_server().reset();
    dead_[i] = true;
  }

  // @safe - construct a fresh server with the retained in-memory storage and
  // install its dispatcher in a fresh worker. Only faults involving `s` are
  // removed, preserving unrelated drops and active partitions.
  void restart(siteid_t s) override {
    const size_t i = index_of(s);
    if (!dead_[i]) return;
    sw_.unisolate_site(s);
    isolated_[i] = false;
    auto receiver = sw_.register_site(s);
    node(s).replace_server(make_server(i));
    start_poll_thread(i);
    workers_[i] = std::make_unique<ChannelNodeWorker>(
        std::move(receiver), node(s).take_dispatcher(), &sw_);
    start_worker(i);
    dead_[i] = false;
  }

  // ------------------------------------------------------------------
  // Each node has a background worker thread draining its channel
  //.
  // ------------------------------------------------------------------

  ~TestCluster() {
    // Drop all senders FIRST so every worker's step_blocking() recv()
    // returns Err and the loop exits. Then join the threads so the
    // detached members (workers_/receivers) aren't destroyed while a
    // worker is mid-recv on them.
    stop_.store(true, std::memory_order_release);
    sw_ = ChannelSwitchboard{};  // move-assign empty; drops all senders
    for (auto& t : worker_threads_) {
      if (t.joinable()) t.join();
    }
    for (size_t i = 0; i < poll_threads_.size(); ++i) {
      stop_poll_thread(i);
    }
    // Dispatchers borrow their RaftServer from RaftNode.  Dispose workers
    // first so no dispatcher survives the server it targets.
    workers_.clear();
    nodes_.clear();
  }

 private:
  // @safe - builds the cluster
  void build(size_t n) {
    site_ids_.reserve(n);
    for (size_t i = 0; i < n; ++i) site_ids_.push_back(static_cast<siteid_t>(i + 1));

    // Register every site with the switchboard; stash receivers
    // for the workers we spin up below.
    std::vector<rusty::sync::mpsc::Receiver<Envelope>> receivers;
    receivers.reserve(n);
    for (auto id : site_ids_) {
      receivers.push_back(sw_.register_site(id));
    }

    // Build the per-site storage + node + worker trio.
    for (size_t i = 0; i < n; ++i) {
      auto id = site_ids_[i];
      // @unsafe { direct `new` because Mutex-containing types are not
      //           copy-constructible, so Box::make() does not apply }
      logs_.emplace_back(std::make_shared<InMemoryLogStorage>());
      snaps_.emplace_back(std::make_shared<MemorySnapshotManager>());

      TransportProxy tr = make_channel_transport(&sw_, id, /*par=*/0);
      rusty::Box<RaftNode> node(new RaftNode(
          id, std::move(tr), logs_.back().get(), snaps_.back().get(),
          make_server(i)));

      auto worker = std::make_unique<ChannelNodeWorker>(
          std::move(receivers[i]), node->take_dispatcher(), &sw_);

      nodes_.push_back(std::move(node));
      workers_.push_back(std::move(worker));
    }

    // Start one reactor and one channel drainer per node only after every
    // real server is initialized, so no election/replication step can see a
    // partially bootstrapped peer set.
    // @unsafe { std::thread at test-harness boundary }
    dead_.assign(n, false);
    isolated_.assign(n, false);
    worker_threads_.resize(n);
    poll_threads_.resize(n);
    poll_thread_generations_.assign(n, 0);
    for (size_t i = 0; i < workers_.size(); ++i) {
      start_poll_thread(i);
      start_worker(i);
    }
  }

  size_t index_of(siteid_t s) const {
    for (size_t i = 0; i < site_ids_.size(); ++i) {
      if (site_ids_[i] == s) return i;
    }
    return site_ids_.size();
  }

  std::unique_ptr<RaftServer> make_server(size_t i) {
    auto server = std::make_unique<RaftServer>(nullptr);
    server->InitializeForInMemoryTest(RaftServerInMemoryTestDependencies{
        site_ids_[i], static_cast<locid_t>(site_ids_[i]), /*partition=*/0,
        site_ids_, make_channel_transport(&sw_, site_ids_[i], /*par=*/0),
        logs_[i], snaps_[i]});
    return server;
  }

  void start_worker(size_t i) {
    ChannelNodeWorker* worker = workers_[i].get();
    worker_threads_[i] = std::thread([worker]() {
      while (worker->step_blocking()) {}
    });
  }

  void stop_worker(size_t i) {
    sw_.unregister_site(site_ids_[i]);
    if (worker_threads_[i].joinable()) worker_threads_[i].join();
    workers_[i].reset();
  }

  void start_poll_thread(size_t i) {
    verify(poll_threads_[i].is_none());
    poll_threads_[i] = rusty::Some(rrr::PollThread::create());
    ++poll_thread_generations_[i];
  }

  // PollThread::shutdown sends CmdShutdown and joins synchronously. It must
  // finish before a server can be replaced or destroyed because its reactor
  // jobs and fibers borrow that server.
  void stop_poll_thread(size_t i) {
    if (poll_threads_[i].is_none()) return;
    poll_threads_[i].as_ref().unwrap()->shutdown();
    poll_threads_[i] = rusty::None;
    ++poll_thread_join_count_;
  }

  // Schedule one deterministic Raft action on a node's reactor and wait for
  // it to finish. The job owns its callback/completion state, so a timeout
  // cannot leave a dangling reference behind.
  bool run_on_poll_thread(size_t i, std::function<void()> action) {
    verify(poll_threads_[i].is_some());
    struct Completion {
      std::atomic<bool> done{false};
    };
    auto completion = std::make_shared<Completion>();
    auto job = rusty::Arc<rrr::OneTimeJob>::new_(rrr::OneTimeJob::new_(
        [action = std::move(action), completion]() mutable {
          action();
          completion->done.store(true, std::memory_order_release);
        }));
    poll_threads_[i].as_ref().unwrap()->add(rusty::Arc<rrr::Job>(job));

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(2);
    while (!completion->done.load(std::memory_order_acquire)) {
      if (std::chrono::steady_clock::now() >= deadline) return false;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
  }

  // Declaration order matters: members are destroyed in REVERSE order,
  // so sw_ must come LAST (destroyed first) to drop its Senders and
  // let every worker's step_blocking() recv() return Err before the
  // workers' Receivers are destroyed. Otherwise detached worker
  // threads UAF on their receivers.
  std::atomic<bool>                              stop_{false};
  std::vector<std::thread>                       worker_threads_;
  std::vector<rusty::Option<rusty::Arc<rrr::PollThread>>> poll_threads_;
  std::vector<size_t>                            poll_thread_generations_;
  size_t                                          poll_thread_join_count_{0};
  std::vector<std::unique_ptr<ChannelNodeWorker>> workers_;
  std::vector<rusty::Box<RaftNode>>              nodes_;
  std::vector<bool>                              dead_;
  std::vector<bool>                              isolated_;
  std::vector<std::shared_ptr<MemorySnapshotManager>> snaps_;
  std::vector<std::shared_ptr<InMemoryLogStorage>>    logs_;
  std::vector<siteid_t>                          site_ids_;
  ChannelSwitchboard                             sw_;
};

}  // namespace raft
}  // namespace janus

#ifdef RAFT_TEST_CORO
#include "testconf.h"

inline janus::RaftTestConfig::RaftTestConfig(
    janus::raft::TestCluster& cluster)
    : cluster_(&cluster) {
  for (auto site : cluster_->site_ids()) {
    disconnected_[site] = false;
  }
}
#endif
