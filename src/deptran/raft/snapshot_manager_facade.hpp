#pragma once

// RaftServer owns this value facade instead of reaching directly into the
// legacy virtual SnapshotManager boundary. The facade leaves streaming object
// ownership with the selected backend and makes that boundary explicit.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "snapshot_manager.hpp"

namespace janus {
namespace raft {

class SnapshotManagerProxy {
 public:
  SnapshotManagerProxy() = default;

  // @unsafe - retains a legacy polymorphic snapshot backend shared with setup.
  explicit SnapshotManagerProxy(std::shared_ptr<SnapshotManager> backend)
      : backend_(std::move(backend)) {}

  explicit operator bool() const noexcept { return backend_ != nullptr; }

  // Keep existing RaftServer call sites source-compatible while dispatching
  // through the facade's forwarding methods rather than the raw backend.
  SnapshotManagerProxy* operator->() noexcept { return this; }
  const SnapshotManagerProxy* operator->() const noexcept { return this; }

  // @unsafe - exposes the legacy backend for compatibility-only callers.
  std::shared_ptr<SnapshotManager> backend() const { return backend_; }

  std::unique_ptr<SnapshotWriter> BeginSnapshot(uint64_t last_index,
                                                 int64_t last_term) {
    return manager().BeginSnapshot(last_index, last_term);
  }
  bool TakeSnapshot(uint64_t last_index, int64_t last_term,
                    const c_char* data, size_t size) {
    return manager().TakeSnapshot(last_index, last_term, data, size);
  }
  std::unique_ptr<SnapshotReader> BeginLoad(const SnapshotMetadata& metadata) {
    return manager().BeginLoad(metadata);
  }
  bool LoadLatestSnapshot(SnapshotMetadata* metadata_out,
                          std::string* data_out) {
    return manager().LoadLatestSnapshot(metadata_out, data_out);
  }
  rusty::Option<SnapshotMetadata> GetLatestSnapshot() const {
    return manager().GetLatestSnapshot();
  }
  std::vector<SnapshotMetadata> ListSnapshots() const {
    return manager().ListSnapshots();
  }
  bool HasSnapshotAtOrAfter(uint64_t min_index) const {
    return manager().HasSnapshotAtOrAfter(min_index);
  }
  size_t PruneSnapshots(uint64_t keep_after_index) {
    return manager().PruneSnapshots(keep_after_index);
  }
  size_t DeleteAllSnapshots() { return manager().DeleteAllSnapshots(); }
  const std::string& GetStoragePath() const {
    return manager().GetStoragePath();
  }

 private:
  SnapshotManager& manager() {
    verify(backend_ != nullptr);
    return *backend_;
  }
  const SnapshotManager& manager() const {
    verify(backend_ != nullptr);
    return *backend_;
  }

  std::shared_ptr<SnapshotManager> backend_;
};

// All existing SnapshotManager implementations use this one adapter point.
inline SnapshotManagerProxy make_snapshot_manager_proxy(
    std::shared_ptr<SnapshotManager> backend) {
  return SnapshotManagerProxy(std::move(backend));
}

}  // namespace raft
}  // namespace janus
