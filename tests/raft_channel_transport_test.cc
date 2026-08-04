// Phase 8.0 smoke test: two sites send RPCs through a ChannelSwitchboard
// to recording dispatchers. Fiber-synchronous — senders block on an
// mpsc reply channel until the remote worker thread produces the reply.
//
// Each node runs a dedicated std::thread calling step_blocking() so the
// sender actually has someone to unblock it.

#include <stdlib.h>

#include <gtest/gtest.h>


#include <rusty/arc.hpp>
#include <rusty/box.hpp>
#include <rusty/sync/atomic.hpp>

#include "deptran/raft/channel_transport.hpp"

import std;

using namespace janus::raft;

namespace {

using AtomicInt = rusty::sync::atomic::detail::Atomic<int>;

struct Counts {
  AtomicInt n_append{0};
  AtomicInt n_vote{0};
  AtomicInt n_timeout{0};
  AtomicInt n_vote_durable{0};
  AtomicInt n_append_durable{0};
  AtomicInt n_notify_restart{0};
  AtomicInt n_install{0};
};

class RecordingDispatcher : public DispatcherBase {
 public:
  rusty::Arc<Counts> counts{rusty::Arc<Counts>::make()};

  VoteReply handle_vote(VoteReq req) override {
    counts->n_vote.fetch_add(1);
    VoteReply r{}; r.max_ballot = req.current_term; r.vote_granted = true; return r;
  }
  VoteDurableReply handle_vote_durable(VoteDurableReq) override {
    counts->n_vote_durable.fetch_add(1);
    return VoteDurableReply{};
  }
  AppendEntriesReply handle_append_entries(AppendEntriesReq req) override {
    counts->n_append.fetch_add(1);
    AppendEntriesReply r{};
    r.follower_append_ok = 1;
    r.follower_current_term = req.leader_current_term;
    r.follower_last_log_index = req.leader_prev_log_index;
    return r;
  }
  EmptyAppendEntriesReply handle_empty_append_entries(EmptyAppendEntriesReq req) override {
    counts->n_append.fetch_add(1);
    EmptyAppendEntriesReply r{};
    // Make the bool boundary observable: this catches a nonzero wire value
    // being lost before it reaches the transport-neutral request.
    r.follower_append_ok = req.trigger_election_now ? 2 : 1;
    r.follower_current_term = req.leader_current_term;
    r.follower_last_log_index = req.leader_prev_log_index;
    return r;
  }
  AppendEntriesDurableReply handle_append_entries_durable(AppendEntriesDurableReq) override {
    counts->n_append_durable.fetch_add(1);
    return AppendEntriesDurableReply{};
  }
  TimeoutNowReply handle_timeout_now(TimeoutNowReq req) override {
    counts->n_timeout.fetch_add(1);
    TimeoutNowReply r{}; r.follower_term = req.leader_term; r.success = true; return r;
  }
  NotifyRestartReply handle_notify_restart(NotifyRestartReq) override {
    counts->n_notify_restart.fetch_add(1);
    return NotifyRestartReply{};
  }
  InstallSnapshotReply handle_install_snapshot(InstallSnapshotReq req) override {
    counts->n_install.fetch_add(1);
    InstallSnapshotReply r{}; r.term_out = req.term; return r;
  }
};

// Spins a std::thread running step_blocking() until a stop flag is set.
struct WorkerHarness {
  std::atomic<bool> stop{false};
  std::thread th;

  // @unsafe { std::thread is on its way out; background worker for tests }
  WorkerHarness(ChannelNodeWorker* w) {
    th = std::thread([w, this] {
      while (!stop.load()) {
        if (!w->step_blocking()) break;  // channel closed
      }
    });
  }

  ~WorkerHarness() {
    stop.store(true);
    if (th.joinable()) th.detach();  // will exit when recv errors on drop
  }
};

}  // namespace

TEST(RaftChannelTransportTest, RoundTripBetweenTwoSites) {
  ChannelSwitchboard sw;
  auto rx_a = sw.register_site(1);
  auto rx_b = sw.register_site(2);

  auto* raw_a = new RecordingDispatcher();
  auto* raw_b = new RecordingDispatcher();
  rusty::Arc<Counts> counts_a = raw_a->counts;
  rusty::Arc<Counts> counts_b = raw_b->counts;
  DispatcherProxy disp_a(raw_a);
  DispatcherProxy disp_b(raw_b);

  TransportProxy tr_a = make_channel_transport(&sw, /*self=*/1, /*par=*/0);
  TransportProxy tr_b = make_channel_transport(&sw, /*self=*/2, /*par=*/0);

  ChannelNodeWorker w_a{std::move(rx_a), std::move(disp_a)};
  ChannelNodeWorker w_b{std::move(rx_b), std::move(disp_b)};

  WorkerHarness ha{&w_a};
  WorkerHarness hb{&w_b};

  auto ae = tr_a->send_append_entries(2, AppendEntriesReq{});
  EXPECT_EQ(ae.follower_append_ok, 1u);

  auto tn = tr_b->send_timeout_now(1, TimeoutNowReq{});
  EXPECT_TRUE(tn.success);

  auto v = tr_a->send_vote(2, VoteReq{});
  EXPECT_TRUE(v.vote_granted);

  tr_a->send_vote_durable(2, VoteDurableReq{});
  tr_a->send_append_entries_durable(2, AppendEntriesDurableReq{});
  // Give the durables a moment to be consumed before we tear down.
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  EXPECT_EQ(counts_a->n_timeout.load(), 1);
  EXPECT_EQ(counts_b->n_append.load(),  1);
  EXPECT_EQ(counts_b->n_vote.load(),    1);
  EXPECT_EQ(counts_b->n_vote_durable.load(), 1);
}

TEST(RaftChannelTransportTest, DropDirectionFallsBackToDefault) {
  ChannelSwitchboard sw;
  auto rx_a = sw.register_site(1);
  auto rx_b = sw.register_site(2);

  auto* raw_a = new RecordingDispatcher();
  auto* raw_b = new RecordingDispatcher();
  rusty::Arc<Counts> counts_b = raw_b->counts;
  DispatcherProxy disp_a(raw_a);
  DispatcherProxy disp_b(raw_b);

  TransportProxy tr_a = make_channel_transport(&sw, 1, 0);

  ChannelNodeWorker w_a{std::move(rx_a), std::move(disp_a)};
  ChannelNodeWorker w_b{std::move(rx_b), std::move(disp_b)};

  WorkerHarness ha{&w_a};
  WorkerHarness hb{&w_b};

  // Drop 1→2; send_timeout_now's envelope is dropped at the switchboard,
  // so the reply sender is destroyed and recv() returns Err. The adapter
  // falls back to a default-constructed reply (success=false).
  sw.drop_direction(/*from=*/1, /*to=*/2);
  auto dropped = tr_a->send_timeout_now(2, TimeoutNowReq{});
  EXPECT_FALSE(dropped.success);

  sw.reset_faults();
  auto ok = tr_a->send_timeout_now(2, TimeoutNowReq{});
  EXPECT_TRUE(ok.success);
  EXPECT_EQ(counts_b->n_timeout.load(), 1);
}

TEST(RaftChannelTransportTest, IsolationDropsAnEnvelopeQueuedBeforeDisconnect) {
  ChannelSwitchboard sw;
  auto rx_b = sw.register_site(2);
  auto* raw_b = new RecordingDispatcher();
  rusty::Arc<Counts> counts_b = raw_b->counts;
  DispatcherProxy disp_b(raw_b);
  ChannelNodeWorker w_b{std::move(rx_b), std::move(disp_b), &sw};
  TransportProxy transport = make_channel_transport(&sw, 1, 0);

  // The durable RPC queues successfully while the link is live. Disconnect
  // before the worker dequeues it: the second isolation check must discard it.
  transport->send_vote_durable(2, VoteDurableReq{7, 1});
  sw.isolate_site(2);
  EXPECT_TRUE(w_b.step());
  EXPECT_EQ(counts_b->n_vote_durable.load(), 0);

  sw.unisolate_site(2);
  transport->send_vote_durable(2, VoteDurableReq{8, 1});
  EXPECT_TRUE(w_b.step());
  EXPECT_EQ(counts_b->n_vote_durable.load(), 1);
}

TEST(RaftChannelTransportTest, UndropRestoresOnlyTheSelectedDirection) {
  ChannelSwitchboard sw;
  auto rx_a = sw.register_site(1);
  auto rx_b = sw.register_site(2);
  auto* raw_a = new RecordingDispatcher();
  auto* raw_b = new RecordingDispatcher();
  DispatcherProxy disp_a(raw_a);
  DispatcherProxy disp_b(raw_b);
  ChannelNodeWorker w_a{std::move(rx_a), std::move(disp_a)};
  ChannelNodeWorker w_b{std::move(rx_b), std::move(disp_b)};
  WorkerHarness ha{&w_a};
  WorkerHarness hb{&w_b};
  TransportProxy from_one = make_channel_transport(&sw, 1, 0);
  TransportProxy from_two = make_channel_transport(&sw, 2, 0);

  sw.drop_direction(1, 2);
  sw.drop_direction(2, 1);
  EXPECT_FALSE(from_one->send_timeout_now(2, TimeoutNowReq{}).success);
  EXPECT_FALSE(from_two->send_timeout_now(1, TimeoutNowReq{}).success);

  sw.undrop_direction(1, 2);
  EXPECT_TRUE(from_one->send_timeout_now(2, TimeoutNowReq{}).success);
  EXPECT_FALSE(from_two->send_timeout_now(1, TimeoutNowReq{}).success);
}

TEST(RaftChannelTransportTest, DroppedReplyRpcsUseDefaultsAndRecover) {
  ChannelSwitchboard sw;
  auto rx_a = sw.register_site(1);
  auto rx_b = sw.register_site(2);
  auto* raw_a = new RecordingDispatcher();
  auto* raw_b = new RecordingDispatcher();
  DispatcherProxy disp_a(raw_a);
  DispatcherProxy disp_b(raw_b);
  ChannelNodeWorker w_a{std::move(rx_a), std::move(disp_a)};
  ChannelNodeWorker w_b{std::move(rx_b), std::move(disp_b)};
  WorkerHarness ha{&w_a};
  WorkerHarness hb{&w_b};
  TransportProxy transport = make_channel_transport(&sw, 1, 0);

  sw.drop_direction(1, 2);
  EXPECT_FALSE(transport->send_vote(2, VoteReq{0, 0, 1, 91}).vote_granted);
  EXPECT_EQ(transport->send_append_entries(
                2, AppendEntriesReq{0, 0, 73, 1, 41, 0, 0, janus::Command{}, 0})
                .follower_append_ok,
            0u);
  EXPECT_EQ(transport->send_empty_append_entries(
                2, EmptyAppendEntriesReq{0, 0, 74, 1, 42, 0, 0, true})
                .follower_append_ok,
            0u);
  EXPECT_FALSE(transport->send_timeout_now(2, TimeoutNowReq{75, 1}).success);
  EXPECT_EQ(transport->send_install_snapshot(
                2, InstallSnapshotReq{76, 1, 0, 0, "snapshot"}).term_out,
            0u);

  sw.reset_faults();
  EXPECT_EQ(transport->send_vote(2, VoteReq{0, 0, 1, 91}).max_ballot, 91u);
  auto append = transport->send_append_entries(
      2, AppendEntriesReq{0, 0, 73, 1, 41, 0, 0, janus::Command{}, 0});
  EXPECT_EQ(append.follower_current_term, 73u);
  EXPECT_EQ(append.follower_last_log_index, 41u);
  auto empty = transport->send_empty_append_entries(
      2, EmptyAppendEntriesReq{0, 0, 74, 1, 42, 0, 0, true});
  EXPECT_EQ(empty.follower_append_ok, 2u);
  EXPECT_EQ(empty.follower_current_term, 74u);
  EXPECT_EQ(transport->send_timeout_now(2, TimeoutNowReq{75, 1}).follower_term, 75u);
  EXPECT_EQ(transport->send_install_snapshot(
                2, InstallSnapshotReq{76, 1, 0, 0, "snapshot"}).term_out,
            76u);
}

TEST(RaftChannelTransportTest, FireAndForgetRpcsRespectFaultsAndRecover) {
  ChannelSwitchboard sw;
  auto rx_a = sw.register_site(1);
  auto rx_b = sw.register_site(2);
  auto* raw_a = new RecordingDispatcher();
  auto* raw_b = new RecordingDispatcher();
  rusty::Arc<Counts> counts_b = raw_b->counts;
  DispatcherProxy disp_a(raw_a);
  DispatcherProxy disp_b(raw_b);
  ChannelNodeWorker w_a{std::move(rx_a), std::move(disp_a)};
  ChannelNodeWorker w_b{std::move(rx_b), std::move(disp_b)};
  WorkerHarness ha{&w_a};
  WorkerHarness hb{&w_b};
  TransportProxy transport = make_channel_transport(&sw, 1, 0);

  sw.drop_direction(1, 2);
  transport->send_vote_durable(2, VoteDurableReq{7, 1});
  transport->send_append_entries_durable(2, AppendEntriesDurableReq{7, 1, 9});
  transport->send_notify_restart(2, 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  EXPECT_EQ(counts_b->n_vote_durable.load(), 0);
  EXPECT_EQ(counts_b->n_append_durable.load(), 0);
  EXPECT_EQ(counts_b->n_notify_restart.load(), 0);

  sw.reset_faults();
  transport->send_vote_durable(2, VoteDurableReq{8, 1});
  transport->send_append_entries_durable(2, AppendEntriesDurableReq{8, 1, 10});
  transport->send_notify_restart(2, 0);
  for (int i = 0; i < 20 && counts_b->n_notify_restart.load() != 1; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  EXPECT_EQ(counts_b->n_vote_durable.load(), 1);
  EXPECT_EQ(counts_b->n_append_durable.load(), 1);
  EXPECT_EQ(counts_b->n_notify_restart.load(), 1);
}
