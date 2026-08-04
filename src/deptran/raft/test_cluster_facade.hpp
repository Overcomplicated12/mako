#pragma once

#include <cstdint>
#include <vector>

#include "../constants.h"

namespace janus {
class RaftServer;

namespace raft {

class TestClusterFacade {
 public:
  virtual ~TestClusterFacade() noexcept(false) = default;

  virtual size_t size() const = 0;
  virtual const std::vector<siteid_t>& site_ids() const = 0;
  virtual RaftServer* node_server(siteid_t site) = 0;
  virtual bool node_is_leader(siteid_t site) const = 0;
  virtual uint64_t node_current_term(siteid_t site) const = 0;
  virtual uint64_t node_commit_index(siteid_t site) const = 0;
  virtual bool node_has_committed_command(siteid_t site, uint64_t index,
                                          int command_id) = 0;
  virtual uint64_t node_rpc_count(siteid_t site) const = 0;

  virtual void disconnect(siteid_t site) = 0;
  virtual void reconnect(siteid_t site) = 0;
  virtual void partition(std::vector<siteid_t> a,
                         std::vector<siteid_t> b) = 0;
  virtual void reset_faults() = 0;
  // Rebuild all in-memory servers from empty volatile storage. This is used
  // between independent lab sections; production-backed configs never expose
  // this operation.
  virtual void reset_for_independent_test_section() = 0;
  virtual void kill(siteid_t site) = 0;
  virtual void restart(siteid_t site) = 0;

  virtual bool step_election(siteid_t candidate = 0) = 0;
  virtual bool step_replication() = 0;
  virtual bool append_command(siteid_t site, int command_id,
                              uint64_t* index, uint64_t* term) = 0;
};

}  // namespace raft
}  // namespace janus
