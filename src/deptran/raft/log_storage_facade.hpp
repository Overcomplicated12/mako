#pragma once

// RaftServer owns this value facade instead of reaching directly into the
// legacy virtual LogStorage boundary. Backends remain polymorphic while the
// server-facing storage contract is explicit and auditable.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "log_storage.hpp"

namespace janus {
namespace raft {

class LogStorageProxy {
 public:
  LogStorageProxy() = default;

  // @unsafe - retains a legacy polymorphic storage backend shared with setup.
  explicit LogStorageProxy(std::shared_ptr<LogStorage> backend)
      : backend_(std::move(backend)) {}

  explicit operator bool() const noexcept { return backend_ != nullptr; }

  // Keep existing RaftServer call sites source-compatible while dispatching
  // through the facade's forwarding methods rather than the raw backend.
  LogStorageProxy* operator->() noexcept { return this; }
  const LogStorageProxy* operator->() const noexcept { return this; }

  // @unsafe - exposes the legacy backend for compatibility-only callers.
  std::shared_ptr<LogStorage> backend() const { return backend_; }

  rusty::Option<LogEntry> get(uint64_t slot_id) const {
    return storage().get(slot_id);
  }
  bool put(const LogEntry& entry) { return storage().put(entry); }
  bool remove(uint64_t slot_id) { return storage().remove(slot_id); }
  std::vector<LogEntry> get_range(uint64_t start, uint64_t end) const {
    return storage().get_range(start, end);
  }
  bool put_batch(const std::vector<LogEntry>& entries) {
    return storage().put_batch(entries);
  }
  bool remove_range(uint64_t start, uint64_t end) {
    return storage().remove_range(start, end);
  }
  uint64_t get_first_index() const { return storage().get_first_index(); }
  uint64_t get_last_index() const { return storage().get_last_index(); }
  rusty::Option<int64_t> get_term(uint64_t slot_id) const {
    return storage().get_term(slot_id);
  }
  size_t size() const { return storage().size(); }
  bool empty() const { return storage().empty(); }
  bool set_metadata(const std::string& key, const std::string& value) {
    return storage().set_metadata(key, value);
  }
  rusty::Option<std::string> get_metadata(const std::string& key) const {
    return storage().get_metadata(key);
  }
  bool sync() { return storage().sync(); }
  bool close() { return storage().close(); }
  bool is_open() const { return storage().is_open(); }
  bool clear() { return storage().clear(); }

 private:
  LogStorage& storage() {
    verify(backend_ != nullptr);
    return *backend_;
  }
  const LogStorage& storage() const {
    verify(backend_ != nullptr);
    return *backend_;
  }

  std::shared_ptr<LogStorage> backend_;
};

// All existing LogStorage implementations use this one adapter point.
inline LogStorageProxy make_log_storage_proxy(
    std::shared_ptr<LogStorage> backend) {
  return LogStorageProxy(std::move(backend));
}

}  // namespace raft
}  // namespace janus
