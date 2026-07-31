#pragma once

/**
 * @file raft_node.hpp
 * @brief Phase 6 of the decouple plan — single-node facade that owns a
 *        transport, storage, and snapshot-manager and exposes a
 *        DispatcherProxy to the cluster. Intentionally a SKELETON: it
 *        holds the wiring but does not yet drive the full RaftServer
 *        state machine, because RaftServer is still coupled to
 *        rrr::PollThread / rrr::Fiber. That integration is the
 *        remaining Phase 6.5 (deferred in the same spirit as Phase 2.5).
 *
 * What this file provides right now:
 *   - RaftNode type holding:
 *       siteid_t id, TransportProxy, LogStorage&, SnapshotManager&,
 *       a simple DispatcherProxy produced by the node itself.
 *   - Inspection accessors (is_leader, current_term, commit_index) —
 *     placeholder implementations backed by in-node fields so tests
 *     can exercise the cluster plumbing end-to-end.
 *   - A dispatcher-injection constructor so the cluster can use a
 *     RaftServer-backed adapter without changing channel-worker ownership.
 *     The convenience constructor below still installs DummyDispatcher until
 *     the cluster constructs real RaftServers in Phase 8.5.
 *
 * The point of keeping this skeleton now is to let Phase 7 wire up
 * raft_lab_standalone without a circular dependency on the RaftServer
 * refactor.
 */

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <rusty/box.hpp>

#include "channel_transport.hpp"
#include "dispatcher.hpp"
#include "log_storage.hpp"
#include "messages.hpp"
#include "raft_server_dispatcher.hpp"
#include "snapshot_manager.hpp"
#include "transport.hpp"

#include "../constants.h"

namespace janus {
namespace raft {

// ---------------------------------------------------------------------------
// DummyDispatcher — placeholder that accepts every RPC and responds.
// Phase 6.5 will swap this for a RaftServer-backed dispatcher.
// ---------------------------------------------------------------------------

#if RUSTYCPP_RUST
pub struct DummyDispatcherCore {
    self_: u16,
}

impl DummyDispatcherCore {
    // @safe
    fn new(self_site: u16) -> DummyDispatcherCore {
        DummyDispatcherCore {
            self_: self_site,
        }
    }

    // @safe
    fn handle_vote(&self, req: VoteReq) -> VoteReply {
        VoteReply {
            max_ballot: req.current_term,
            vote_granted: true,
        }
    }

    // @safe
    fn handle_vote_durable(&self, _req: VoteDurableReq) -> VoteDurableReply {
        VoteDurableReply {
            acknowledged: true,
        }
    }

    // @safe
    fn handle_append_entries(&self, _req: AppendEntriesReq) -> AppendEntriesReply {
        AppendEntriesReply {
            follower_append_ok: 1,
            follower_current_term: 0,
            follower_last_log_index: 0,
            follower_ack_type: 0,
        }
    }

    // @safe
    fn handle_empty_append_entries(&self, _req: EmptyAppendEntriesReq)
        -> EmptyAppendEntriesReply {
        EmptyAppendEntriesReply {
            follower_append_ok: 1,
            follower_current_term: 0,
            follower_last_log_index: 0,
            follower_ack_type: 0,
        }
    }

    // @safe
    fn handle_append_entries_durable(&self, _req: AppendEntriesDurableReq)
        -> AppendEntriesDurableReply {
        AppendEntriesDurableReply {
            acknowledged: true,
        }
    }

    // @safe
    fn handle_timeout_now(&self, _req: TimeoutNowReq) -> TimeoutNowReply {
        TimeoutNowReply {
            follower_term: 0,
            success: true,
        }
    }

    // @safe
    fn handle_notify_restart(&self, _req: NotifyRestartReq) -> NotifyRestartReply {
        NotifyRestartReply {
            acknowledged: true,
        }
    }

    // @safe
    fn handle_install_snapshot(&self, _req: InstallSnapshotReq) -> InstallSnapshotReply {
        InstallSnapshotReply {
            term_out: 0,
        }
    }

    // @safe
    fn self_site_id(&self) -> u16 {
        self.self_
    }
}
#endif
/*RUSTYCPP:GEN-BEGIN id=raft_node.dummy_dispatcher_core version=1 rust_sha256=007abf3dd95e2a16974e50914d9863580efa2a33a3b950c795288d0396af58f2*/
struct DummyDispatcherCore;

struct DummyDispatcherCore {
    uint16_t self_;

    static DummyDispatcherCore new_(uint16_t self_site);
    VoteReply handle_vote(VoteReq req) const;
    VoteDurableReply handle_vote_durable(VoteDurableReq _req) const;
    AppendEntriesReply handle_append_entries(AppendEntriesReq _req) const;
    EmptyAppendEntriesReply handle_empty_append_entries(EmptyAppendEntriesReq _req) const;
    AppendEntriesDurableReply handle_append_entries_durable(AppendEntriesDurableReq _req) const;
    TimeoutNowReply handle_timeout_now(TimeoutNowReq _req) const;
    NotifyRestartReply handle_notify_restart(NotifyRestartReq _req) const;
    InstallSnapshotReply handle_install_snapshot(InstallSnapshotReq _req) const;
    uint16_t self_site_id() const;
};


inline DummyDispatcherCore DummyDispatcherCore::new_(uint16_t self_site) {
    return DummyDispatcherCore{.self_ = std::move(self_site)};
}

inline VoteReply DummyDispatcherCore::handle_vote(VoteReq req) const {
    return VoteReply{.max_ballot = std::move(req.current_term), .vote_granted = true};
}

inline VoteDurableReply DummyDispatcherCore::handle_vote_durable(VoteDurableReq _req) const {
    return VoteDurableReply{.acknowledged = true};
}

inline AppendEntriesReply DummyDispatcherCore::handle_append_entries(AppendEntriesReq _req) const {
    return AppendEntriesReply{.follower_append_ok = 1, .follower_current_term = 0, .follower_last_log_index = 0, .follower_ack_type = 0};
}

inline EmptyAppendEntriesReply DummyDispatcherCore::handle_empty_append_entries(EmptyAppendEntriesReq _req) const {
    return EmptyAppendEntriesReply{.follower_append_ok = 1, .follower_current_term = 0, .follower_last_log_index = 0, .follower_ack_type = 0};
}

inline AppendEntriesDurableReply DummyDispatcherCore::handle_append_entries_durable(AppendEntriesDurableReq _req) const {
    return AppendEntriesDurableReply{.acknowledged = true};
}

inline TimeoutNowReply DummyDispatcherCore::handle_timeout_now(TimeoutNowReq _req) const {
    return TimeoutNowReply{.follower_term = 0, .success = true};
}

inline NotifyRestartReply DummyDispatcherCore::handle_notify_restart(NotifyRestartReq _req) const {
    return NotifyRestartReply{.acknowledged = true};
}

inline InstallSnapshotReply DummyDispatcherCore::handle_install_snapshot(InstallSnapshotReq _req) const {
    return InstallSnapshotReply{.term_out = 0};
}

inline uint16_t DummyDispatcherCore::self_site_id() const {
    return this->self_;
}
/*RUSTYCPP:GEN-END id=raft_node.dummy_dispatcher_core*/

class DummyDispatcher : public DispatcherBase {
 public:
  // @safe
  explicit DummyDispatcher(siteid_t self)
      : core_(DummyDispatcherCore::new_(self)) {}

  VoteReply handle_vote(VoteReq req) override {
    return core_.handle_vote(std::move(req));
  }
  VoteDurableReply handle_vote_durable(VoteDurableReq req) override {
    return core_.handle_vote_durable(std::move(req));
  }
  AppendEntriesReply handle_append_entries(AppendEntriesReq req) override {
    return core_.handle_append_entries(std::move(req));
  }
  EmptyAppendEntriesReply handle_empty_append_entries(EmptyAppendEntriesReq req) override {
    return core_.handle_empty_append_entries(std::move(req));
  }
  AppendEntriesDurableReply handle_append_entries_durable(AppendEntriesDurableReq req) override {
    return core_.handle_append_entries_durable(std::move(req));
  }
  TimeoutNowReply handle_timeout_now(TimeoutNowReq req) override {
    return core_.handle_timeout_now(std::move(req));
  }
  NotifyRestartReply handle_notify_restart(NotifyRestartReq req) override {
    return core_.handle_notify_restart(std::move(req));
  }
  InstallSnapshotReply handle_install_snapshot(InstallSnapshotReq req) override {
    return core_.handle_install_snapshot(std::move(req));
  }

  siteid_t self_site_id() const { return core_.self_site_id(); }

 private:
  DummyDispatcherCore core_;
};

#if RUSTYCPP_RUST
pub struct RaftNodeStateCore {
    id_: u16,
    is_leader_: rusty::Cell<bool>,
    commit_index_: rusty::Cell<u64>,
    current_term_: rusty::Cell<u64>,
}

impl RaftNodeStateCore {
    // @safe
    fn new(id: u16) -> RaftNodeStateCore {
        RaftNodeStateCore {
            id_: id,
            is_leader_: rusty::Cell::<bool>::new_(false),
            commit_index_: rusty::Cell::<u64>::new_(0),
            current_term_: rusty::Cell::<u64>::new_(0),
        }
    }

    // @safe
    fn id(&self) -> u16 {
        self.id_
    }

    // @safe
    fn is_leader(&self) -> bool {
        self.is_leader_.get()
    }

    // @safe
    fn set_is_leader(&mut self, value: bool) {
        self.is_leader_.set(value)
    }

    // @safe
    fn commit_index(&self) -> u64 {
        self.commit_index_.get()
    }

    // @safe
    fn set_commit_index(&mut self, value: u64) {
        self.commit_index_.set(value)
    }

    // @safe
    fn current_term(&self) -> u64 {
        self.current_term_.get()
    }

    // @safe
    fn set_current_term(&mut self, value: u64) {
        self.current_term_.set(value)
    }
}
#endif
/*RUSTYCPP:GEN-BEGIN id=raft_node.2 version=1 rust_sha256=a17dd16282342a82fc08decde755f495313988dc342d12136327ed2826fdad07*/
struct RaftNodeStateCore;

struct RaftNodeStateCore {
    uint16_t id_;
    rusty::Cell<bool> is_leader_;
    rusty::Cell<uint64_t> commit_index_;
    rusty::Cell<uint64_t> current_term_;

    static RaftNodeStateCore new_(uint16_t id);
    uint16_t id() const;
    bool is_leader() const;
    void set_is_leader(bool value);
    uint64_t commit_index() const;
    void set_commit_index(uint64_t value);
    uint64_t current_term() const;
    void set_current_term(uint64_t value);
};


inline RaftNodeStateCore RaftNodeStateCore::new_(uint16_t id) {
    return RaftNodeStateCore{.id_ = std::move(id), .is_leader_ = rusty::Cell<bool>::new_(false), .commit_index_ = rusty::Cell<uint64_t>::new_(static_cast<uint64_t>(0)), .current_term_ = rusty::Cell<uint64_t>::new_(static_cast<uint64_t>(0))};
}

inline uint16_t RaftNodeStateCore::id() const {
    return this->id_;
}

inline bool RaftNodeStateCore::is_leader() const {
    return this->is_leader_.get();
}

inline void RaftNodeStateCore::set_is_leader(bool value) {
    this->is_leader_.set(std::move(value));
}

inline uint64_t RaftNodeStateCore::commit_index() const {
    return this->commit_index_.get();
}

inline void RaftNodeStateCore::set_commit_index(uint64_t value) {
    this->commit_index_.set(std::move(value));
}

inline uint64_t RaftNodeStateCore::current_term() const {
    return this->current_term_.get();
}

inline void RaftNodeStateCore::set_current_term(uint64_t value) {
    this->current_term_.set(std::move(value));
}
/*RUSTYCPP:GEN-END id=raft_node.2*/
// ---------------------------------------------------------------------------
// RaftNode — owns transport + storage + dispatcher for one site.
// ---------------------------------------------------------------------------

class RaftNode {
 public:
  // @unsafe { log_storage and snap_manager are non-owning references;
  //           their lifetimes are managed by TestCluster (phase 6) or
  //           by the production wiring (later). }
  RaftNode(siteid_t id,
           TransportProxy transport,
           LogStorage* log_storage,
           SnapshotManager* snap_manager)
      : RaftNode(id, std::move(transport), log_storage, snap_manager,
                 rusty::make_box<DummyDispatcher>(id)) {}

  // @unsafe { `dispatcher` may borrow a RaftServer. The owner of that server
  // must outlive the ChannelNodeWorker which takes this dispatcher. }
  RaftNode(siteid_t id,
           TransportProxy transport,
           LogStorage* log_storage,
           SnapshotManager* snap_manager,
           DispatcherProxy dispatcher)
      : state_core_(RaftNodeStateCore::new_(id)),
        transport_(std::move(transport)),
        log_storage_(log_storage),
        snap_manager_(snap_manager),
        dispatcher_(std::move(dispatcher)) {}

  // @unsafe { server ownership is retained by the node while its dispatcher
  // is owned by the worker. TestCluster destroys workers before nodes. }
  RaftNode(siteid_t id,
           TransportProxy transport,
           LogStorage* log_storage,
           SnapshotManager* snap_manager,
           std::unique_ptr<RaftServer> server)
      : state_core_(RaftNodeStateCore::new_(id)),
        transport_(std::move(transport)),
        log_storage_(log_storage),
        snap_manager_(snap_manager),
        dispatcher_(make_raft_server_dispatcher(server.get())),
        server_(std::move(server)) {}

  // @safe
  siteid_t id() const { return state_core_.id(); }

  // DispatcherProxy is move-only. The node transfers it exactly once to its
  // ChannelNodeWorker; keeping that ownership boundary explicit means a real
  // RaftServer-backed dispatcher has the same lifetime model as the dummy.
  // @unsafe { caller must take the dispatcher only once. }
  DispatcherProxy take_dispatcher() {
    return std::move(dispatcher_);
  }

  // Inspection accessors. These are placeholders backed by simple
  // in-node fields so test-cluster plumbing can be exercised; they
  // will be replaced by delegation to a real RaftServer in Phase 6.5.
  // @safe
  bool is_leader() const {
    return server_ ? server_->IsLeader() : state_core_.is_leader();
  }
  slotid_t commit_index() const {
    return server_ ? server_->commitIndex : state_core_.commit_index();
  }
  ballot_t current_term() const {
    return server_ ? server_->currentTerm : state_core_.current_term();
  }

  // @safe - borrowed server pointer, null only for the legacy dummy node.
  RaftServer* server() { return server_.get(); }

  // @safe - manual state injection used by the Phase 6 tests
  void force_leader(bool b) {
    if (server_) {
      server_->setIsLeader(b);
    } else {
      state_core_.set_is_leader(b);
    }
  }
  void set_commit_index(slotid_t s) {
    if (server_) {
      server_->commitIndex = s;
    } else {
      state_core_.set_commit_index(s);
    }
  }
  void set_current_term(ballot_t t) {
    if (server_) {
      server_->currentTerm = t;
    } else {
      state_core_.set_current_term(t);
    }
  }

  // @safe - borrow the transport for sending RPCs
  TransportProxy& transport() { return transport_; }

  // @safe
  LogStorage*      log_storage()     { return log_storage_; }
  SnapshotManager* snapshot_manager(){ return snap_manager_; }

 private:
  RaftNodeStateCore             state_core_;
  TransportProxy                transport_;
  LogStorage*                   log_storage_{nullptr};
  SnapshotManager*              snap_manager_{nullptr};
  DispatcherProxy               dispatcher_;
  std::unique_ptr<RaftServer>   server_{};
};

}  // namespace raft
}  // namespace janus
