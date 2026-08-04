#pragma once

/**
 * @file raft_node.hpp
 * @brief In-process Raft node facade. Each TestCluster node owns a real
 *        RaftServer and transfers the server-backed DispatcherProxy exactly
 *        once to its ChannelNodeWorker. The small injection-only constructor
 *        remains for ownership-boundary unit tests.
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
}

impl RaftNodeStateCore {
    // @safe
    fn new(id: u16) -> RaftNodeStateCore {
        RaftNodeStateCore {
            id_: id,
        }
    }

    // @safe
    fn id(&self) -> u16 {
        self.id_
    }

}
#endif
/*RUSTYCPP:GEN-BEGIN id=raft_node.2 version=1 rust_sha256=61862082a5bf70ffccff3aa9b531565d7d1e6504e8a24002709e2b436172b9b0*/
struct RaftNodeStateCore;

struct RaftNodeStateCore {
    uint16_t id_;

    static RaftNodeStateCore new_(uint16_t id);
    uint16_t id() const;
};


inline RaftNodeStateCore RaftNodeStateCore::new_(uint16_t id) {
    return RaftNodeStateCore{.id_ = std::move(id)};
}

inline uint16_t RaftNodeStateCore::id() const {
    return this->id_;
}
/*RUSTYCPP:GEN-END id=raft_node.2*/
// ---------------------------------------------------------------------------
// RaftNode — owns transport + storage + dispatcher for one site.
// ---------------------------------------------------------------------------

class RaftNode {
 public:
  // @unsafe { `dispatcher` may borrow a RaftServer. The owner of that server
  // must outlive the ChannelNodeWorker which takes this dispatcher. This
  // constructor is only the explicit dispatcher-ownership test seam. }
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

  // @safe - real cluster nodes delegate inspection to their owned server.
  bool is_leader() const {
    return server_ != nullptr && server_->IsLeader();
  }
  slotid_t commit_index() const {
    return server_ != nullptr ? server_->commitIndex : 0;
  }
  ballot_t current_term() const {
    return server_ != nullptr ? server_->currentTerm : 0;
  }

  // @safe - borrowed server pointer, null only for the legacy dummy node.
  RaftServer* server() { return server_.get(); }

  // @safe - test hooks for real cluster nodes.
  void force_leader(bool b) {
    if (server_) {
      server_->setIsLeader(b);
    }
  }
  void set_commit_index(slotid_t s) {
    if (server_) {
      server_->commitIndex = s;
    }
  }
  void set_current_term(ballot_t t) {
    if (server_) {
      server_->currentTerm = t;
    }
  }

  // @unsafe - caller must first stop and join the worker that owns the old
  // dispatcher. The returned server stays alive until that ordering is met.
  std::unique_ptr<RaftServer> replace_server(std::unique_ptr<RaftServer> server) {
    verify(server != nullptr);
    dispatcher_ = make_raft_server_dispatcher(server.get());
    auto old = std::move(server_);
    server_ = std::move(server);
    return old;
  }

  // @unsafe - the caller must first stop the worker and join the node's
  // PollThread. This is the terminal half of the TestCluster kill lifecycle.
  std::unique_ptr<RaftServer> release_server() { return std::move(server_); }

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
