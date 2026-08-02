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
 * Elections and replication are stepped deterministically by tests; each RPC
 * still crosses the real channel transport and RaftServerDispatcher boundary.
 */

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include <rusty/arc.hpp>
#include <rusty/box.hpp>

#include "channel_transport.hpp"
#include "../classic/tpc_command.h"
#include "memory_log_storage.hpp"
#include "memory_snapshot_manager.hpp"
#include "raft_node.hpp"

#include "../constants.h"

namespace janus {
namespace raft {

class TestCluster {
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
  size_t size() const { return nodes_.size(); }
  RaftNode& node(siteid_t id) {
    for (auto& n : nodes_) {
      if (n->id() == id) return *n;
    }
    // @unsafe { bogus id — abort via out-of-range dereference }
    return *nodes_.at(nodes_.size());  // throws std::out_of_range
  }
  ChannelSwitchboard& switchboard() { return sw_; }

  // @safe - returns the full site-id list.
  const std::vector<siteid_t>& site_ids() const { return site_ids_; }

  // ------------------------------------------------------------------
  // Fault injection
  // ------------------------------------------------------------------

  // @safe - stops all traffic from `s` to every peer and vice versa.
  void disconnect(siteid_t s) {
    for (auto peer : site_ids_) {
      if (peer == s) continue;
      sw_.drop_direction(s, peer);
      sw_.drop_direction(peer, s);
    }
  }

  // @safe - splits sites into two groups that cannot exchange
  // messages across the boundary. Sites outside both groups remain
  // fully connected (via current switchboard semantics).
  void partition(std::vector<siteid_t> a, std::vector<siteid_t> b) {
    sw_.partition({std::move(a), std::move(b)});
  }

  // @safe - clear all fault injections.
  void reset_faults() { sw_.reset_faults(); }

  // @safe - starts an election on the first live node. One explicit step keeps
  // elections deterministic in tests without starting production timer fibers.
  bool step_election() {
    for (size_t i = 0; i < nodes_.size(); ++i) {
      if (!dead_[i]) return nodes_[i]->server()->StartElectionForInMemoryTest();
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

  // @safe - append a stateless command through the elected real server.
  bool append_noop_to_leader(uint64_t* index) {
    for (size_t i = 0; i < nodes_.size(); ++i) {
      if (dead_[i] || !nodes_[i]->is_leader()) continue;
      auto command = rusty::Arc<TpcNoopCommand>::make();
      janus::Command envelope =
          janus::Command::pack_aliased<TpcNoopCommand>(std::move(command));
      uint64_t term = 0;
      return nodes_[i]->server()->Start(envelope, index, &term);
    }
    return false;
  }

  // @safe - drive one heartbeat/append round on each live server. Only the
  // leader sends traffic; other calls are no-ops.
  void step_replication() {
    for (size_t i = 0; i < nodes_.size(); ++i) {
      if (!dead_[i]) (void)nodes_[i]->server()->DriveReplicationOnceForInMemoryTest();
    }
  }

  // @safe - close and join the worker before its dispatcher can outlive the
  // server. The server itself is released during restart, after this ordering.
  void kill(siteid_t s) {
    const size_t i = index_of(s);
    if (dead_[i]) return;
    node(s).force_leader(false);
    disconnect(s);
    stop_worker(i);
    dead_[i] = true;
  }

  // @safe - construct a fresh server with the retained in-memory storage and
  // install its dispatcher in a fresh worker. Only faults involving `s` are
  // removed, preserving unrelated drops and active partitions.
  void restart(siteid_t s) {
    const size_t i = index_of(s);
    if (!dead_[i]) return;
    for (auto peer : site_ids_) {
      if (peer == s) continue;
      sw_.undrop_direction(s, peer);
      sw_.undrop_direction(peer, s);
    }
    auto receiver = sw_.register_site(s);
    auto retired = node(s).replace_server(make_server(i));
    retired.reset();
    workers_[i] = std::make_unique<ChannelNodeWorker>(
        std::move(receiver), node(s).take_dispatcher());
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
          std::move(receivers[i]), node->take_dispatcher());

      nodes_.push_back(std::move(node));
      workers_.push_back(std::move(worker));
    }

    // Spawn one background drainer per node.
    // @unsafe { std::thread at test-harness boundary }
    dead_.assign(n, false);
    worker_threads_.resize(n);
    for (size_t i = 0; i < workers_.size(); ++i) start_worker(i);
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

  // Declaration order matters: members are destroyed in REVERSE order,
  // so sw_ must come LAST (destroyed first) to drop its Senders and
  // let every worker's step_blocking() recv() return Err before the
  // workers' Receivers are destroyed. Otherwise detached worker
  // threads UAF on their receivers.
  std::atomic<bool>                              stop_{false};
  std::vector<std::thread>                       worker_threads_;
  std::vector<std::unique_ptr<ChannelNodeWorker>> workers_;
  std::vector<rusty::Box<RaftNode>>              nodes_;
  std::vector<bool>                              dead_;
  std::vector<std::shared_ptr<MemorySnapshotManager>> snaps_;
  std::vector<std::shared_ptr<InMemoryLogStorage>>    logs_;
  std::vector<siteid_t>                          site_ids_;
  ChannelSwitchboard                             sw_;
};

}  // namespace raft
}  // namespace janus
