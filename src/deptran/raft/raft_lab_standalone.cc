// In-process driver for the Phase 8.7 Raft lab contract. It uses real
// RaftServers, channel transport, and in-memory storage only: no sockets,
// production Frame, ReplicatedDB, or RocksDB bootstrap is created.

#include <cstdio>

#include "deptran/raft/test_cluster.hpp"
#include "deptran/raft/test.h"

int main(int /*argc*/, char** /*argv*/) {
#ifdef RAFT_TEST_CORO
  auto cluster = janus::raft::TestCluster::with_in_memory_transport(5);
  janus::RaftTestConfig config(*cluster);
  janus::RaftLabTest test(&config);

  const int result = test.Run();
  test.Cleanup();
  return result == 0 ? 0 : 1;
#else
  std::fputs("raft_lab_standalone requires a RAFT_TEST=ON build\n", stderr);
  return 2;
#endif
}
