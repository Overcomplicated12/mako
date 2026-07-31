// Focused RaftServerDispatcher checks. A null server is an intentional
// lifecycle state during Kill/Restart and must preserve service-level reply
// defaults without needing to construct a full RaftServer.

#include <gtest/gtest.h>

#include "deptran/raft/raft_server_dispatcher.hpp"

using namespace janus::raft;

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
