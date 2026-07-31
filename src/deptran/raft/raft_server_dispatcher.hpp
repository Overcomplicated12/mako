#pragma once

// Adapter from the transport-neutral inbound RPC facade to RaftServer's
// existing pointer-out handler API. This is deliberately a C++ boundary:
// RaftServer owns consensus state and the dispatcher only packs/unpacks the
// transport-neutral request and reply values.

#include "dispatcher.hpp"
#include "server.h"

namespace janus {
namespace raft {

class RaftServerDispatcher final : public DispatcherBase {
 public:
  // @unsafe - stores a borrowed RaftServer pointer. The caller must keep the
  // server alive while this dispatcher can receive work.
  explicit RaftServerDispatcher(RaftServer* svr) : svr_(svr) {}

  VoteReply handle_vote(VoteReq req) override {
    VoteReply reply{};
    if (unavailable()) {
      reply.max_ballot = req.current_term;
      reply.vote_granted = false;
      return reply;
    }
    bool_t vote_granted = false;
    svr_->OnRequestVote(req.last_log_idx, req.last_log_term,
                        req.candidate_site_id, req.current_term,
                        &reply.max_ballot, &vote_granted);
    reply.vote_granted = vote_granted;
    return reply;
  }

  VoteDurableReply handle_vote_durable(VoteDurableReq req) override {
    VoteDurableReply reply{};
    if (unavailable()) {
      reply.acknowledged = false;
      return reply;
    }
    bool_t acknowledged = false;
    svr_->OnVoteDurable(req.term, req.voter_id, &acknowledged);
    reply.acknowledged = acknowledged;
    return reply;
  }

  AppendEntriesReply handle_append_entries(AppendEntriesReq req) override {
    AppendEntriesReply reply{};
    reply.follower_ack_type = 0;  // memory acknowledgement, matching service.
    if (unavailable()) {
      return reply;
    }
    svr_->OnAppendEntries(req.slot, req.ballot, req.leader_current_term,
                          req.leader_site_id, req.leader_prev_log_index,
                          req.leader_prev_log_term, req.leader_commit_index,
                          req.cmd, req.leader_next_log_term,
                          &reply.follower_append_ok,
                          &reply.follower_current_term,
                          &reply.follower_last_log_index);
    return reply;
  }

  EmptyAppendEntriesReply handle_empty_append_entries(
      EmptyAppendEntriesReq req) override {
    EmptyAppendEntriesReply reply{};
    reply.follower_ack_type = 0;  // memory acknowledgement, matching service.
    if (unavailable()) {
      return reply;
    }
    svr_->OnAppendEntries(req.slot, req.ballot, req.leader_current_term,
                          req.leader_site_id, req.leader_prev_log_index,
                          req.leader_prev_log_term, req.leader_commit_index,
                          janus::Command{}, 0, &reply.follower_append_ok,
                          &reply.follower_current_term,
                          &reply.follower_last_log_index,
                          req.trigger_election_now);
    return reply;
  }

  AppendEntriesDurableReply handle_append_entries_durable(
      AppendEntriesDurableReq req) override {
    AppendEntriesDurableReply reply{};
    if (unavailable()) {
      reply.acknowledged = false;
      return reply;
    }
    bool_t acknowledged = false;
    svr_->OnAppendEntriesDurable(req.term, req.follower_id,
                                 req.last_log_index, &acknowledged);
    reply.acknowledged = acknowledged;
    return reply;
  }

  TimeoutNowReply handle_timeout_now(TimeoutNowReq req) override {
    TimeoutNowReply reply{};
    if (unavailable()) {
      reply.follower_term = 0;
      reply.success = false;
      return reply;
    }
    bool_t success = false;
    svr_->OnTimeoutNow(req.leader_term, req.leader_site_id,
                       &reply.follower_term, &success);
    reply.success = success;
    return reply;
  }

  NotifyRestartReply handle_notify_restart(NotifyRestartReq req) override {
    NotifyRestartReply reply{};
    if (unavailable()) {
      reply.acknowledged = false;
      return reply;
    }

    auto commo = svr_->commo();
    if (commo != nullptr) {
      reply.acknowledged = commo->ReconnectToSite(req.restarted_site_id,
                                                  svr_->partition_id_);
    }

    // Always invalidate this peer's speculative state, even if there is no
    // communicator to reconnect. This mirrors RaftServiceImpl::NotifyRestart.
    svr_->OnPeerRestart(req.restarted_site_id);
    return reply;
  }

  InstallSnapshotReply handle_install_snapshot(InstallSnapshotReq req) override {
    InstallSnapshotReply reply{};
    if (unavailable()) {
      reply.term_out = 0;
      return reply;
    }
    svr_->OnInstallSnapshot(req.term, req.leader_id, req.last_included_index,
                            req.last_included_term, req.data, &reply.term_out);
    return reply;
  }

 private:
  bool unavailable() const {
    return svr_ == nullptr || svr_->IsDisconnected();
  }

  RaftServer* svr_;
};

// @unsafe - the returned proxy borrows `svr`; see RaftServerDispatcher.
inline DispatcherProxy make_raft_server_dispatcher(RaftServer* svr) {
  return rusty::make_box<RaftServerDispatcher>(svr);
}

}  // namespace raft
}  // namespace janus
