// In-process real-RaftServer coverage: stand up a 3-node cluster, send RPCs
// through ChannelTransportAdapter, and step deterministic election and
// replication rounds without the production Frame/Config bootstrap.
//
// Each node runs a background drainer thread inside TestCluster, so
// senders just call `transport()->send_x(dst, req)` and receive the
// reply as a return value.

#include <gtest/gtest.h>

#include "deptran/raft/test_cluster.hpp"

using namespace janus::raft;

namespace {

class RejectingDispatcher final : public DispatcherBase {
 public:
  VoteReply handle_vote(VoteReq req) override {
    return VoteReply{req.current_term, false};
  }
  VoteDurableReply handle_vote_durable(VoteDurableReq) override { return {}; }
  AppendEntriesReply handle_append_entries(AppendEntriesReq) override { return {}; }
  EmptyAppendEntriesReply handle_empty_append_entries(
      EmptyAppendEntriesReq) override { return {}; }
  AppendEntriesDurableReply handle_append_entries_durable(
      AppendEntriesDurableReq) override { return {}; }
  TimeoutNowReply handle_timeout_now(TimeoutNowReq) override { return {}; }
  NotifyRestartReply handle_notify_restart(NotifyRestartReq) override { return {}; }
  InstallSnapshotReply handle_install_snapshot(InstallSnapshotReq) override { return {}; }
};

}  // namespace

TEST(RaftTestClusterTest, BuildAndSendAVote) {
  auto c = TestCluster::with_in_memory_transport(3);
  EXPECT_EQ(c->size(), 3u);
  EXPECT_EQ(c->site_ids().size(), 3u);

  // Node 1 votes peer 2 through the real RaftServerDispatcher.
  auto r = c->node(1).transport()->send_vote(2, VoteReq{1, 0, 1, 1});
  EXPECT_TRUE(r.vote_granted);
}

TEST(RaftTestClusterTest, NodeTransfersAnInjectedDispatcher) {
  ChannelSwitchboard switchboard;
  InMemoryLogStorage log;
  MemorySnapshotManager snapshot;
  RaftNode node(1, make_channel_transport(&switchboard, 1, 0), &log,
                &snapshot, rusty::make_box<RejectingDispatcher>());

  auto dispatcher = node.take_dispatcher();
  auto reply = dispatcher->handle_vote(VoteReq{0, 0, 2, 17});
  EXPECT_FALSE(reply.vote_granted);
  EXPECT_EQ(reply.max_ballot, 17u);
}

TEST(RaftTestClusterTest, DisconnectStopsTraffic) {
  auto c = TestCluster::with_in_memory_transport(3);
  c->disconnect(2);

  // 1→2 is dropped at the switchboard; reply channel closes and the
  // append RPC falls back to its zero-value reply.
  auto dropped = c->node(1).transport()->send_empty_append_entries(
      2, EmptyAppendEntriesReq{0, 0, 0, 1, 0, 0, 0, false});
  EXPECT_EQ(dropped.follower_append_ok, 0u);
  // 1→3 still works (not on the drop list).
  auto ok3 = c->node(1).transport()->send_empty_append_entries(
      3, EmptyAppendEntriesReq{0, 0, 0, 1, 0, 0, 0, false});
  EXPECT_EQ(ok3.follower_append_ok, 1u);

  c->reset_faults();
  auto ok2 = c->node(1).transport()->send_empty_append_entries(
      2, EmptyAppendEntriesReq{0, 0, 0, 1, 0, 0, 0, false});
  EXPECT_EQ(ok2.follower_append_ok, 1u);
}

TEST(RaftTestClusterTest, PartitionIsolatesGroups) {
  auto c = TestCluster::with_in_memory_transport(5);
  c->partition({1, 2}, {3, 4, 5});

  // 1→3: across partition, dropped.
  auto cross = c->node(1).transport()->send_empty_append_entries(
      3, EmptyAppendEntriesReq{0, 0, 0, 1, 0, 0, 0, false});
  EXPECT_EQ(cross.follower_append_ok, 0u);
  // 1→2: same partition, delivered.
  auto intra = c->node(1).transport()->send_empty_append_entries(
      2, EmptyAppendEntriesReq{0, 0, 0, 1, 0, 0, 0, false});
  EXPECT_EQ(intra.follower_append_ok, 1u);
}

TEST(RaftTestClusterTest, InspectionAccessors) {
  auto c = TestCluster::with_in_memory_transport(3);
  c->node(1).set_current_term(42);
  c->node(1).set_commit_index(10);

  EXPECT_EQ(c->node(1).current_term(), 42u);
  EXPECT_EQ(c->node(1).commit_index(), 10u);
  EXPECT_FALSE(c->node(2).is_leader());
}

TEST(RaftTestClusterTest, ElectionConvergesOnExactlyOneLeader) {
  auto c = TestCluster::with_in_memory_transport(3);

  ASSERT_TRUE(c->step_election());
  EXPECT_EQ(c->leader_count(), 1u);
}

TEST(RaftTestClusterTest, AgreementAdvancesCommitIndexOnEveryNode) {
  auto c = TestCluster::with_in_memory_transport(3);
  ASSERT_TRUE(c->step_election());

  uint64_t index = 0;
  ASSERT_TRUE(c->append_noop_to_leader(&index));
  ASSERT_GT(index, 0u);

  // First round replicates the entry and commits it on the leader. The second
  // conveys the new leader commit index to followers.
  c->step_replication();
  c->step_replication();
  for (auto id : c->site_ids()) {
    EXPECT_GE(c->node(id).commit_index(), index) << "site " << id;
  }
}

TEST(RaftTestClusterTest, DisconnectedFollowerCatchesUpOnlyAfterReset) {
  auto c = TestCluster::with_in_memory_transport(3);
  ASSERT_TRUE(c->step_election());
  c->disconnect(3);

  uint64_t index = 0;
  ASSERT_TRUE(c->append_noop_to_leader(&index));
  c->step_replication();
  c->step_replication();
  EXPECT_LT(c->node(3).commit_index(), index);

  c->reset_faults();
  c->step_replication();
  c->step_replication();
  EXPECT_GE(c->node(3).commit_index(), index);
}

TEST(RaftTestClusterTest, RestartPreservesUnrelatedDirectedDrop) {
  auto c = TestCluster::with_in_memory_transport(3);
  c->switchboard().drop_direction(1, 3);

  c->kill(2);
  c->restart(2);

  auto dropped = c->node(1).transport()->send_empty_append_entries(
      3, EmptyAppendEntriesReq{0, 0, 0, 1, 0, 0, 0, false});
  EXPECT_EQ(dropped.follower_append_ok, 0u);
}
