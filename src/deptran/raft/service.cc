#include "service.h"
#include "service_test_hooks.hpp"
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
  std::unique_lock<std::mutex> lease(server_lifecycle_mutex_);
#ifdef RAFT_TEST_CORO
  void* hook_context = nullptr;
  BeforeDispatchHook hook = nullptr;
  {
    std::lock_guard<std::mutex> lock(dispatch_hook_mutex_);
    hook_context = before_dispatch_hook_context_;
    hook = before_dispatch_hook_;
  }
  if (hook) hook(hook_context);
#endif
  auto dispatcher = raft::make_raft_server_dispatcher(state_core_.server());
  auto reply = dispatcher->handle_vote(
      raft::VoteReq{req.lst_log_idx, req.lst_log_term, req.site_id, req.cur_term});
  RpcVoteResponse resp{};
  resp.max_ballot = reply.max_ballot;
  resp.vote_granted = reply.vote_granted;
  return Result<RpcVoteResponse, rrr::i32>::Ok(resp);
}

Result<RaftService::RpcVoteDurableResponse, rrr::i32>
RaftServiceImpl::VoteDurable(const RpcVoteDurableRequest& req) {
  std::unique_lock<std::mutex> lease(server_lifecycle_mutex_);
  auto dispatcher = raft::make_raft_server_dispatcher(state_core_.server());
  auto reply = dispatcher->handle_vote_durable(
      raft::VoteDurableReq{req.term, req.voter_id});
  RpcVoteDurableResponse resp{};
  resp.acknowledged = reply.acknowledged;
  return Result<RpcVoteDurableResponse, rrr::i32>::Ok(resp);
}

Result<RaftService::RpcAppendEntriesResponse, rrr::i32>
RaftServiceImpl::AppendEntries(const RpcAppendEntriesRequest& req) {
  std::unique_lock<std::mutex> lease(server_lifecycle_mutex_);
  auto dispatcher = raft::make_raft_server_dispatcher(state_core_.server());
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
  std::unique_lock<std::mutex> lease(server_lifecycle_mutex_);
  auto dispatcher = raft::make_raft_server_dispatcher(state_core_.server());
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
  std::unique_lock<std::mutex> lease(server_lifecycle_mutex_);
  auto dispatcher = raft::make_raft_server_dispatcher(state_core_.server());
  auto reply = dispatcher->handle_append_entries_durable(
      raft::AppendEntriesDurableReq{req.term, req.follower_id, req.lastLogIndex});
  RpcAppendEntriesDurableResponse resp{};
  resp.acknowledged = reply.acknowledged;
  return Result<RpcAppendEntriesDurableResponse, rrr::i32>::Ok(resp);
}

Result<RaftService::RpcTimeoutNowResponse, rrr::i32>
RaftServiceImpl::TimeoutNow(const RpcTimeoutNowRequest& req) {
  std::unique_lock<std::mutex> lease(server_lifecycle_mutex_);
  auto dispatcher = raft::make_raft_server_dispatcher(state_core_.server());
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
  std::unique_lock<std::mutex> lease(server_lifecycle_mutex_);
  auto dispatcher = raft::make_raft_server_dispatcher(state_core_.server());
  auto reply = dispatcher->handle_notify_restart(
      raft::NotifyRestartReq{req.restartedSiteId});
  RpcNotifyRestartResponse resp{};
  resp.acknowledged = reply.acknowledged;
  return Result<RpcNotifyRestartResponse, rrr::i32>::Ok(resp);
}

Result<RaftService::RpcInstallSnapshotResponse, rrr::i32>
RaftServiceImpl::InstallSnapshot(const RpcInstallSnapshotRequest& req) {
  std::unique_lock<std::mutex> lease(server_lifecycle_mutex_);
  auto dispatcher = raft::make_raft_server_dispatcher(state_core_.server());
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
  std::unique_lock<std::mutex> lease(server_lifecycle_mutex_);
  RaftServer* svr = state_core_.server();
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
  std::unique_lock<std::mutex> lease(server_lifecycle_mutex_);
  RaftServer* svr = state_core_.server();
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

rusty::Mutex<rusty::BTreeMap<siteid_t, RaftServiceImpl*>>
    RaftServiceImpl::service_registry_{
        rusty::BTreeMap<siteid_t, RaftServiceImpl*>::new_()};

// @unsafe - C-style cast from scheduler base to borrowed RaftServer pointer.
// The service stores the pointer atomically but does not own the server.
RaftServiceImpl::RaftServiceImpl(TxLogServer *sched, rusty::Arc<rrr::PollThread> poll_thread)
    : state_core_(RaftServiceStateCore::new_((RaftServer*)sched,
                                             ((RaftServer*)sched)->site_id_,
                                             std::move(poll_thread))) {
  // @unsafe
  RaftServer* svr = (RaftServer*)sched;
  {
    auto registry = service_registry_.lock().unwrap();
    registry->insert(state_core_.site_id(), this);
  }
  struct timespec curr_time;
  clock_gettime(CLOCK_MONOTONIC_RAW, &curr_time);
  srand(curr_time.tv_nsec);
}

RaftServiceImpl::~RaftServiceImpl() {
  auto registry = service_registry_.lock().unwrap();
  auto service = registry->get(state_core_.site_id());
  if (service.is_some() && service.unwrap() == this) {
    // BTreeMap::remove currently cannot return a raw-pointer value through
    // the transpiled port. Keep the key but clear the borrowed pointer.
    registry->insert(state_core_.site_id(), nullptr);
  }
}

void RaftServiceImpl::UpdateServer(siteid_t site_id, RaftServer* new_svr) {
  auto registry = service_registry_.lock().unwrap();
  auto service = registry->get(site_id);
  if (service.is_some() && service.unwrap() != nullptr) {
    // Wait for every in-flight handler using the old borrowed pointer before
    // publishing nullptr/replacement. Kill() may destroy the old frame as
    // soon as this returns, so acquire/release alone is not sufficient.
    auto* target = service.unwrap();
    std::unique_lock<std::mutex> server_lock(
        target->server_lifecycle_mutex_);
    target->state_core_.set_server(new_svr);
    Log_info("[RAFT-SERVICE] UpdateServer: site {} -> {}", site_id, (void*)new_svr);
  } else {
    Log_warn("[RAFT-SERVICE] UpdateServer: site {} not found in registry", site_id);
  }
}

RaftServer* RaftServiceImpl::GetServer() {
  // This is an inspection-only atomic snapshot. RPC handlers acquire a
  // lifecycle lease before loading and dereferencing the same pointer.
  return state_core_.server();
}

#ifdef RAFT_TEST_CORO
void RaftServiceImpl::SetBeforeDispatchHookForTest(void* context,
                                                   void (*hook)(void*)) {
  std::lock_guard<std::mutex> lock(dispatch_hook_mutex_);
  before_dispatch_hook_context_ = context;
  before_dispatch_hook_ = hook;
}

namespace {

struct ServiceUpdateTestGate {
  std::atomic<bool> entered{false};
  std::atomic<bool> release{false};
};

void block_service_dispatch_for_update_test(void* context) {
  auto* gate = static_cast<ServiceUpdateTestGate*>(context);
  gate->entered.store(true, std::memory_order_release);
  while (!gate->release.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
}

}  // namespace

bool raft::run_raft_service_update_server_in_flight_test(
    RaftServer* first, RaftServer* replacement) {
  if (first == nullptr || replacement == nullptr) {
    return false;
  }

  auto poll_thread = rrr::PollThread::create();
  RaftServiceImpl service(static_cast<TxLogServer*>(first), poll_thread.clone());
  RaftService::RpcVoteRequest request{};
  request.lst_log_idx = 0;
  request.lst_log_term = 0;
  request.site_id = 2;
  request.cur_term = 3;

  ServiceUpdateTestGate gate;
  service.SetBeforeDispatchHookForTest(&gate, &block_service_dispatch_for_update_test);

  uint64_t in_flight_term = 0;
  bool in_flight_ok = false;
  std::thread rpc([&] {
    auto response = service.Vote(request);
    if (response.is_ok()) {
      in_flight_term = response.unwrap().max_ballot;
      in_flight_ok = true;
    }
  });
  while (!gate.entered.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }

  std::atomic<bool> update_finished{false};
  std::thread kill([&] {
    RaftServiceImpl::UpdateServer(first->site_id_, nullptr);
    update_finished.store(true, std::memory_order_release);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  const bool update_waited_for_dispatch =
      !update_finished.load(std::memory_order_acquire);

  gate.release.store(true, std::memory_order_release);
  rpc.join();
  kill.join();

  service.SetBeforeDispatchHookForTest(nullptr, nullptr);
  auto down = service.Vote(request);
  const bool down_ok = down.is_ok() && down.unwrap().max_ballot == 3 &&
                       !down.unwrap().vote_granted;

  RaftServiceImpl::UpdateServer(first->site_id_, replacement);
  auto after_restart = service.Vote(request);
  const bool replacement_ok =
      after_restart.is_ok() && after_restart.unwrap().max_ballot == 11 &&
      !after_restart.unwrap().vote_granted;

  return in_flight_ok && in_flight_term == 7 && update_waited_for_dispatch &&
         update_finished.load(std::memory_order_acquire) && down_ok &&
         replacement_ok;
}
#endif

rusty::Option<rusty::Arc<rrr::PollThread>>
RaftServiceImpl::GetPollThread(siteid_t site_id) {
  auto registry = service_registry_.lock().unwrap();
  auto service = registry->get(site_id);
  if (raft_service_poll_thread_available(
          service.is_some(),
          service.is_some() && service.unwrap() != nullptr &&
              service.unwrap()->state_core_.has_poll_thread())) {
    return service.unwrap()->state_core_.clone_poll_thread();
  }
  return rusty::None;
}

} // namespace janus
