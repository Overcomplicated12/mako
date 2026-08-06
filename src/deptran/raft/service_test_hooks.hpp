#pragma once

namespace janus {
class RaftServer;

namespace raft {

#ifdef RAFT_TEST_CORO
// Runs the service-owned half of the Kill/Restart race against two live
// in-memory servers. Kept out of the gtest translation unit because the
// legacy RPC service header cannot be textually imported with Clang 22 BMIs.
bool run_raft_service_update_server_in_flight_test(RaftServer* first,
                                                   RaftServer* replacement);
#endif

}  // namespace raft
}  // namespace janus
