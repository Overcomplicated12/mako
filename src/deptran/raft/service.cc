#include "service.h"
#include "raft_server_dispatcher.hpp"

#include "rrr/rrr.hpp"
#include <rusty/slice.hpp>

// @external: {
//   Log_info:   [safe, (...) -> void]
//   Log_debug:  [safe, (...) -> void]
//   Log_warn:   [safe, (...) -> void]
//   Log_error:  [safe, (...) -> void]
//   verify:     [safe, (...) -> void]
//   clock_gettime: [safe, (...) -> int]
//   srand:      [safe, (...) -> void]
// }

namespace janus {

using rusty::Result;

#if RUSTYCPP_RUST
pub fn raft_service_server_unavailable(has_server: bool,
                                       disconnected: bool) -> bool {
    !has_server || disconnected
}

pub fn raft_service_default_config_success() -> bool {
    false
}

pub fn raft_service_default_leader_hint() -> u64 {
    0
}

pub fn raft_service_poll_thread_available(found: bool,
                                          has_poll_thread: bool) -> bool {
    found && has_poll_thread
}
#endif
/*RUSTYCPP:GEN-BEGIN id=service.1 version=1 rust_sha256=d6b4f4c07cacc4498f30aa1427536ca90b81dbe64e20d8821b9337cec8ad2dd4*/
bool raft_service_server_unavailable(bool has_server, bool disconnected);
bool raft_service_default_config_success();
uint64_t raft_service_default_leader_hint();
bool raft_service_poll_thread_available(bool found, bool has_poll_thread);

bool raft_service_server_unavailable(bool has_server, bool disconnected) {
    return !has_server || rusty::detail::deref_if_pointer_like(disconnected);
}

bool raft_service_default_config_success() {
    return false;
}

uint64_t raft_service_default_leader_hint() {
    return static_cast<uint64_t>(0);
}

bool raft_service_poll_thread_available(bool found, bool has_poll_thread) {
    return rusty::detail::deref_if_pointer_like(found) && rusty::detail::deref_if_pointer_like(has_poll_thread);
}
/*RUSTYCPP:GEN-END id=service.1*/

// =====================================================================
// Fiber-RPC handlers.
//
// Each method here is invoked by the rrr-generated wrapper on a fresh
// Fiber (see src/rrr/pylib/simplerpcgen/lang_cpp.py). We do synchronous
// work — including any PersistState fsync from RaftServer — and return
// the response struct by value. The framework marshals and sends the
// reply when the fiber completes; no DeferredReply anywhere.
//
// Each handler converts the rrr wire payload at this boundary and delegates
// it to a short-lived RaftServerDispatcher built from the current atomic
// borrowed server pointer. The adapter owns the disconnected defaults and
// returns a value reply, so we deliberately do NOT return Err(...): peers
// treat nonzero error codes as a dropped reply.
// =====================================================================

Result<RaftService::RpcVoteResponse, rrr::i32>
RaftServiceImpl::Vote(const RpcVoteRequest& req) {
  auto dispatcher = raft::make_raft_server_dispatcher(GetServer());
  auto reply = dispatcher->handle_vote(
      raft::VoteReq{req.lst_log_idx, req.lst_log_term, req.site_id, req.cur_term});
  RpcVoteResponse resp{};
  resp.max_ballot = reply.max_ballot;
  resp.vote_granted = reply.vote_granted;
  return Result<RpcVoteResponse, rrr::i32>::Ok(resp);
}

Result<RaftService::RpcVoteDurableResponse, rrr::i32>
RaftServiceImpl::VoteDurable(const RpcVoteDurableRequest& req) {
  auto dispatcher = raft::make_raft_server_dispatcher(GetServer());
  auto reply = dispatcher->handle_vote_durable(
      raft::VoteDurableReq{req.term, req.voter_id});
  RpcVoteDurableResponse resp{};
  resp.acknowledged = reply.acknowledged;
  return Result<RpcVoteDurableResponse, rrr::i32>::Ok(resp);
}

Result<RaftService::RpcAppendEntriesResponse, rrr::i32>
RaftServiceImpl::AppendEntries(const RpcAppendEntriesRequest& req) {
  auto dispatcher = raft::make_raft_server_dispatcher(GetServer());
  auto reply = dispatcher->handle_append_entries(raft::AppendEntriesReq{
      req.slot, req.ballot, req.leaderCurrentTerm, req.leaderSiteId,
      req.leaderPrevLogIndex, req.leaderPrevLogTerm, req.leaderCommitIndex,
      req.cmd, req.leaderNextLogTerm});
  RpcAppendEntriesResponse resp{};
  resp.followerAppendOK = reply.follower_append_ok;
  resp.followerCurrentTerm = reply.follower_current_term;
  resp.followerLastLogIndex = reply.follower_last_log_index;
  resp.followerAckType = reply.follower_ack_type;
  return Result<RpcAppendEntriesResponse, rrr::i32>::Ok(resp);
}

Result<RaftService::RpcEmptyAppendEntriesResponse, rrr::i32>
RaftServiceImpl::EmptyAppendEntries(const RpcEmptyAppendEntriesRequest& req) {
  Log_debug("RaftServiceImpl: EmptyAppendEntries answering leader {}", req.leaderSiteId);
  auto dispatcher = raft::make_raft_server_dispatcher(GetServer());
  auto reply = dispatcher->handle_empty_append_entries(
      raft::EmptyAppendEntriesReq{
          req.slot, req.ballot, req.leaderCurrentTerm, req.leaderSiteId,
          req.leaderPrevLogIndex, req.leaderPrevLogTerm, req.leaderCommitIndex,
          static_cast<bool>(req.trigger_election_now)});
  RpcEmptyAppendEntriesResponse resp{};
  resp.followerAppendOK = reply.follower_append_ok;
  resp.followerCurrentTerm = reply.follower_current_term;
  resp.followerLastLogIndex = reply.follower_last_log_index;
  resp.followerAckType = reply.follower_ack_type;
  return Result<RpcEmptyAppendEntriesResponse, rrr::i32>::Ok(resp);
}

Result<RaftService::RpcAppendEntriesDurableResponse, rrr::i32>
RaftServiceImpl::AppendEntriesDurable(const RpcAppendEntriesDurableRequest& req) {
  auto dispatcher = raft::make_raft_server_dispatcher(GetServer());
  auto reply = dispatcher->handle_append_entries_durable(
      raft::AppendEntriesDurableReq{req.term, req.follower_id, req.lastLogIndex});
  RpcAppendEntriesDurableResponse resp{};
  resp.acknowledged = reply.acknowledged;
  return Result<RpcAppendEntriesDurableResponse, rrr::i32>::Ok(resp);
}

Result<RaftService::RpcTimeoutNowResponse, rrr::i32>
RaftServiceImpl::TimeoutNow(const RpcTimeoutNowRequest& req) {
  auto dispatcher = raft::make_raft_server_dispatcher(GetServer());
  auto reply = dispatcher->handle_timeout_now(
      raft::TimeoutNowReq{req.leaderTerm, req.leaderSiteId});
  RpcTimeoutNowResponse resp{};
  resp.followerTerm = reply.follower_term;
  resp.success = reply.success;
  return Result<RpcTimeoutNowResponse, rrr::i32>::Ok(resp);
}

Result<RaftService::RpcNotifyRestartResponse, rrr::i32>
RaftServiceImpl::NotifyRestart(const RpcNotifyRestartRequest& req) {
  Log_info("[NOTIFY-RESTART] Received restart notification from site {}",
           req.restartedSiteId);
  auto dispatcher = raft::make_raft_server_dispatcher(GetServer());
  auto reply = dispatcher->handle_notify_restart(
      raft::NotifyRestartReq{req.restartedSiteId});
  RpcNotifyRestartResponse resp{};
  resp.acknowledged = reply.acknowledged;
  return Result<RpcNotifyRestartResponse, rrr::i32>::Ok(resp);
}

Result<RaftService::RpcInstallSnapshotResponse, rrr::i32>
RaftServiceImpl::InstallSnapshot(const RpcInstallSnapshotRequest& req) {
  auto dispatcher = raft::make_raft_server_dispatcher(GetServer());
  auto reply = dispatcher->handle_install_snapshot(
      raft::InstallSnapshotReq{req.term, req.leader_id, req.last_included_index,
                               req.last_included_term, req.data});
  RpcInstallSnapshotResponse resp{};
  resp.term_out = reply.term_out;
  return Result<RpcInstallSnapshotResponse, rrr::i32>::Ok(resp);
}

Result<RaftService::RpcAddServerResponse, rrr::i32>
RaftServiceImpl::AddServer(const RpcAddServerRequest& req) {
  RpcAddServerResponse resp{};
  RaftServer* svr = GetServer();
  bool has_server = svr != nullptr;
  bool disconnected = has_server && svr->IsDisconnected();
  if (raft_service_server_unavailable(has_server, disconnected)) {
    resp.success = raft_service_default_config_success();
    resp.error_msg = "server down";
    resp.leader_hint = raft_service_default_leader_hint();
    return Result<RpcAddServerResponse, rrr::i32>::Ok(resp);
  }
  svr->OnAddServer(req.term, req.new_server_id, req.new_server_addr,
                   &resp.success, &resp.error_msg, &resp.leader_hint);
  return Result<RpcAddServerResponse, rrr::i32>::Ok(resp);
}

Result<RaftService::RpcRemoveServerResponse, rrr::i32>
RaftServiceImpl::RemoveServer(const RpcRemoveServerRequest& req) {
  RpcRemoveServerResponse resp{};
  RaftServer* svr = GetServer();
  bool has_server = svr != nullptr;
  bool disconnected = has_server && svr->IsDisconnected();
  if (raft_service_server_unavailable(has_server, disconnected)) {
    resp.success = raft_service_default_config_success();
    resp.error_msg = "server down";
    resp.leader_hint = raft_service_default_leader_hint();
    return Result<RpcRemoveServerResponse, rrr::i32>::Ok(resp);
  }
  svr->OnRemoveServer(req.term, req.server_id,
                      &resp.success, &resp.error_msg, &resp.leader_hint);
  return Result<RpcRemoveServerResponse, rrr::i32>::Ok(resp);
}

// =====================================================================
// Registry + lifecycle plumbing.
//
// RaftServiceImpl instances are owned by the rrr::Server after registration.
// The registry stores borrowed service pointers so the RAFT_TEST_CORO
// Kill/Restart harness can swap the borrowed RaftServer pointer without
// rebuilding the RPC service or poll thread.
// =====================================================================

std::map<siteid_t, RaftServiceImpl*> RaftServiceImpl::service_registry_;
std::mutex RaftServiceImpl::registry_mutex_;

// @unsafe - C-style cast from scheduler base to borrowed RaftServer pointer.
// The service stores the pointer atomically but does not own the server.
RaftServiceImpl::RaftServiceImpl(TxLogServer *sched, rusty::Arc<rrr::PollThread> poll_thread)
    : state_core_(RaftServiceStateCore::new_((RaftServer*)sched,
                                             ((RaftServer*)sched)->site_id_,
                                             std::move(poll_thread))) {
  // @unsafe
  RaftServer* svr = (RaftServer*)sched;
  {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    service_registry_[state_core_.site_id()] = this;
  }
  struct timespec curr_time;
  clock_gettime(CLOCK_MONOTONIC_RAW, &curr_time);
  srand(curr_time.tv_nsec);
}

void RaftServiceImpl::UpdateServer(siteid_t site_id, RaftServer* new_svr) {
  std::lock_guard<std::mutex> lock(registry_mutex_);
  auto it = service_registry_.find(site_id);
  if (it != service_registry_.end()) {
    // Publish a borrowed server pointer for future RPC handlers. nullptr is
    // intentional during Kill(); handlers then return disconnected defaults.
    it->second->state_core_.set_server(new_svr);
    Log_info("[RAFT-SERVICE] UpdateServer: site {} -> {}", site_id, (void*)new_svr);
  } else {
    Log_warn("[RAFT-SERVICE] UpdateServer: site {} not found in registry", site_id);
  }
}

RaftServer* RaftServiceImpl::GetServer() {
  // Borrowed pointer load paired with UpdateServer's release-store. The caller
  // must null-check before dereferencing because Kill() publishes nullptr.
  return state_core_.server();
}

rusty::Option<rusty::Arc<rrr::PollThread>>
RaftServiceImpl::GetPollThread(siteid_t site_id) {
  std::lock_guard<std::mutex> lock(registry_mutex_);
  auto it = service_registry_.find(site_id);
  if (raft_service_poll_thread_available(
          it != service_registry_.end(),
          it != service_registry_.end() && it->second->state_core_.has_poll_thread())) {
    return it->second->state_core_.clone_poll_thread();
  }
  return rusty::None;
}

} // namespace janus
