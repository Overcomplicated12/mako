// Focused RaftServerDispatcher checks. A null server is an intentional
// lifecycle state during Kill/Restart and must preserve service-level reply
// defaults. The live cases use the in-process cluster's real RaftServer so
// they cover the non-virtual RaftServer::OnX calls made by the adapter.

#include <gtest/gtest.h>

#include "deptran/raft/service_test_hooks.hpp"
#include "deptran/raft/test_cluster.hpp"
#include "deptran/raft/raft_server_dispatcher.hpp"

using namespace janus;
using namespace janus::raft;

namespace {

class RaftServerDispatcherLiveTest : public ::testing::Test {
 protected:
  RaftServerDispatcherLiveTest()
      : cluster_(TestCluster::with_in_memory_transport(3)) {}

  RaftServer& server() { return *cluster_->node(1).server(); }

  DispatcherProxy dispatcher() {
    return make_raft_server_dispatcher(&server());
  }

  void ElectServerOne() {
    ASSERT_TRUE(server().StartElectionForInMemoryTest());
    ASSERT_TRUE(server().IsLeader());
    ASSERT_TRUE(server().GetSpecVoters().contains(2));
  }

  void SetReconnectResult(bool result) {
    reconnect_result_ = result;
    reconnect_calls_ = 0;
    server().SetReconnectToSiteForTest(this, &ReconnectForTest);
  }

  void ClearReconnectResult() {
    server().SetReconnectToSiteForTest(nullptr, nullptr);
  }

  static bool ReconnectForTest(void* context, siteid_t site_id,
                               parid_t partition_id) {
    auto* test = static_cast<RaftServerDispatcherLiveTest*>(context);
    EXPECT_EQ(site_id, 2);
    EXPECT_EQ(partition_id, 0u);
    ++test->reconnect_calls_;
    return test->reconnect_result_;
  }

  int reconnect_calls() const { return reconnect_calls_; }

 private:
  rusty::Box<TestCluster> cluster_;
  bool reconnect_result_ = false;
  int reconnect_calls_ = 0;
};

TEST_F(RaftServerDispatcherLiveTest, NotifyRestartReconnectSuccessInvalidatesPeerVote) {
  ElectServerOne();
  SetReconnectResult(true);

  auto reply = dispatcher()->handle_notify_restart(NotifyRestartReq{.restarted_site_id = 2});

  EXPECT_TRUE(reply.acknowledged);
  EXPECT_EQ(reconnect_calls(), 1);
  EXPECT_FALSE(server().GetSpecVoters().contains(2));
  ClearReconnectResult();
}

TEST_F(RaftServerDispatcherLiveTest, NotifyRestartReconnectFailureStillInvalidatesPeerVote) {
  ElectServerOne();
  SetReconnectResult(false);

  auto reply = dispatcher()->handle_notify_restart(NotifyRestartReq{.restarted_site_id = 2});

  EXPECT_FALSE(reply.acknowledged);
  EXPECT_EQ(reconnect_calls(), 1);
  EXPECT_FALSE(server().GetSpecVoters().contains(2));
  ClearReconnectResult();
}

TEST_F(RaftServerDispatcherLiveTest, NotifyRestartWithoutCommoStillInvalidatesPeerVote) {
  ElectServerOne();
  ASSERT_EQ(server().commo(), nullptr);

  auto reply = dispatcher()->handle_notify_restart(NotifyRestartReq{.restarted_site_id = 2});

  EXPECT_FALSE(reply.acknowledged);
  EXPECT_FALSE(server().GetSpecVoters().contains(2));
}

TEST_F(RaftServerDispatcherLiveTest, EmptyAppendEntriesPreservesBothElectionFlags) {
  server().currentTerm = 5;
  const EmptyAppendEntriesReq heartbeat{
      .leader_current_term = 5,
      .leader_site_id = 2,
      .leader_prev_log_index = 0,
      .leader_prev_log_term = 0,
      .leader_commit_index = 0,
      .trigger_election_now = false,
  };

  auto ordinary = dispatcher()->handle_empty_append_entries(heartbeat);
  EXPECT_EQ(ordinary.follower_append_ok, 1u);
  EXPECT_EQ(ordinary.follower_current_term, 5u);

  auto transfer = heartbeat;
  transfer.trigger_election_now = true;
  auto triggered = dispatcher()->handle_empty_append_entries(transfer);
  EXPECT_EQ(triggered.follower_append_ok, 1u);
  EXPECT_EQ(triggered.follower_current_term, 5u);
  EXPECT_EQ(server().currentTerm, 5u);
}

TEST_F(RaftServerDispatcherLiveTest, AppendEntriesRejectsStaleTermAndConflictingPrefix) {
  server().currentTerm = 5;

  auto stale = dispatcher()->handle_append_entries(AppendEntriesReq{
      .leader_current_term = 4,
      .leader_site_id = 2,
      .leader_prev_log_index = 0,
      .leader_prev_log_term = 0,
  });
  EXPECT_EQ(stale.follower_append_ok, 0u);
  EXPECT_EQ(stale.follower_current_term, 5u);

  auto conflicting_prefix = dispatcher()->handle_append_entries(AppendEntriesReq{
      .leader_current_term = 5,
      .leader_site_id = 2,
      .leader_prev_log_index = 1,
      .leader_prev_log_term = 99,
  });
  EXPECT_EQ(conflicting_prefix.follower_append_ok, 0u);
  EXPECT_EQ(conflicting_prefix.follower_current_term, 5u);
  EXPECT_EQ(conflicting_prefix.follower_last_log_index, 0u);
}

TEST_F(RaftServerDispatcherLiveTest, InstallSnapshotRejectsStaleTermAndAcceptsEqualTerm) {
  server().currentTerm = 7;

  auto stale = dispatcher()->handle_install_snapshot(InstallSnapshotReq{
      .term = 6,
      .leader_id = 2,
      .last_included_index = 4,
      .last_included_term = 3,
      .data = "stale",
  });
  EXPECT_EQ(stale.term_out, 7u);
  EXPECT_EQ(server().GetSnapshotIndex(), 0u);

  auto equal = dispatcher()->handle_install_snapshot(InstallSnapshotReq{
      .term = 7,
      .leader_id = 2,
      .last_included_index = 4,
      .last_included_term = 3,
      .data = "current",
  });
  EXPECT_EQ(equal.term_out, 7u);
  EXPECT_TRUE(server().HasSnapshot());
  EXPECT_EQ(server().GetSnapshotIndex(), 4u);
  EXPECT_EQ(server().GetSnapshotTerm(), 3u);
}

TEST_F(RaftServerDispatcherLiveTest, TimeoutNowRejectsStaleTermWithoutStartingElection) {
  server().currentTerm = 8;

  auto reply = dispatcher()->handle_timeout_now(
      TimeoutNowReq{.leader_term = 7, .leader_site_id = 2});

  EXPECT_FALSE(reply.success);
  EXPECT_EQ(reply.follower_term, 8u);
  EXPECT_EQ(server().currentTerm, 8u);
  EXPECT_FALSE(server().IsLeader());
}

TEST(RaftServiceLifecycleTest, KillRestartWaitsForInFlightDispatchAndUsesReplacement) {
  auto first_cluster = TestCluster::with_in_memory_transport(3);
  auto replacement_cluster = TestCluster::with_in_memory_transport(3);
  RaftServer& first = *first_cluster->node(1).server();
  RaftServer& replacement = *replacement_cluster->node(1).server();
  first.currentTerm = 7;
  replacement.currentTerm = 11;

  EXPECT_TRUE(raft::run_raft_service_update_server_in_flight_test(
      &first, &replacement));
}

}  // namespace

TEST(RaftServerDispatcherTest, NullServerUsesServiceFailureDefaults) {
  auto dispatcher = make_raft_server_dispatcher(nullptr);

  auto vote = dispatcher->handle_vote(VoteReq{.current_term = 17});
  EXPECT_EQ(vote.max_ballot, 17);
  EXPECT_FALSE(vote.vote_granted);

  EXPECT_FALSE(dispatcher->handle_vote_durable(VoteDurableReq{}).acknowledged);

  auto append = dispatcher->handle_append_entries(AppendEntriesReq{});
  EXPECT_EQ(append.follower_append_ok, 0u);
  EXPECT_EQ(append.follower_current_term, 0u);
  EXPECT_EQ(append.follower_last_log_index, 0u);
  EXPECT_EQ(append.follower_ack_type, 0u);

  auto empty = dispatcher->handle_empty_append_entries(EmptyAppendEntriesReq{});
  EXPECT_EQ(empty.follower_append_ok, 0u);
  EXPECT_EQ(empty.follower_current_term, 0u);
  EXPECT_EQ(empty.follower_last_log_index, 0u);
  EXPECT_EQ(empty.follower_ack_type, 0u);

  EXPECT_FALSE(
      dispatcher->handle_append_entries_durable(AppendEntriesDurableReq{}).acknowledged);

  auto timeout = dispatcher->handle_timeout_now(TimeoutNowReq{});
  EXPECT_EQ(timeout.follower_term, 0u);
  EXPECT_FALSE(timeout.success);

  EXPECT_FALSE(dispatcher->handle_notify_restart(NotifyRestartReq{}).acknowledged);
  EXPECT_EQ(dispatcher->handle_install_snapshot(InstallSnapshotReq{}).term_out, 0u);
}
