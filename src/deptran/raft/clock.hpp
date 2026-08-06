#pragma once

/**
 * @file clock.hpp
 * @brief Injectable monotonic time source for Raft-owned timeout decisions.
 *
 * The production clock is deliberately a tiny adapter around rrr::Time.
 * Tests may instead share a ManualRaftClock and advance it explicitly.  The
 * clock owns no scheduling state: it changes what a Raft timeout observes,
 * not when a production fiber wakes up.
 */

#include <cstdint>
#include <utility>

#include <rusty/arc.hpp>
#include <rusty/box.hpp>
#include <rusty/sync/atomic.hpp>

#include "rrr/rrr.hpp"

namespace janus {
namespace raft {

// ---------------------------------------------------------------------------
// RaftClock / RaftClockProxy
// ---------------------------------------------------------------------------

#if RUSTYCPP_RUST
pub trait RaftClock {
    // @safe - Returns an opaque, monotonic microsecond timestamp.
    fn now_us(&self) -> u64;
}
#endif
/*RUSTYCPP:GEN-BEGIN id=raft_clock.1 version=1 rust_sha256=17b3f2b11035c46efc996d94b8d52a1060f70af2edbf1813a0c3620e14358994*/
namespace {
class RaftClock {
public:
    virtual ~RaftClock() noexcept(false) {}
    virtual uint64_t now_us() const = 0;
    RaftClock(const RaftClock&) = delete;
    RaftClock& operator=(const RaftClock&) = delete;
    RaftClock(RaftClock&&) = delete;
    RaftClock& operator=(RaftClock&&) = delete;
protected:
    RaftClock() = default;
};
}

template <class U> class RaftClockAdapter;
template <class U> class RaftClockAdapterRef;
template <class U> class RaftClockAdapterRefMut;
/*RUSTYCPP:GEN-END id=raft_clock.1*/

// Move-only, server-owned view of a clock implementation.
using RaftClockProxy = rusty::Box<RaftClock>;

/** Production adapter.  This is the only Raft clock code that reads rrr's
 * system time directly. */
class SystemRaftClock final : public RaftClock {
 public:
  // @unsafe - rrr's system clock is outside the Rusty ownership model.
  uint64_t now_us() const override { return rrr::Time::now(false); }
};

/**
 * Shared test clock.  It has an explicit initial epoch and can only move
 * forward.  Acquire/AcqRel ordering publishes test-thread advances to timer
 * checks executing on PollThreads.
 */
class ManualRaftClock final {
 public:
  explicit ManualRaftClock(uint64_t initial_us) : now_us_(initial_us) {}

  uint64_t now_us() const {
    return now_us_.load(rusty::sync::atomic::Ordering::Acquire);
  }

  uint64_t advance_by_us(uint64_t delta_us) const {
    return now_us_.fetch_add(
               delta_us, rusty::sync::atomic::Ordering::AcqRel) +
           delta_us;
  }

 private:
  rusty::sync::atomic::AtomicU64 now_us_;
};

namespace detail {

class ManualRaftClockAdapter final : public RaftClock {
 public:
  explicit ManualRaftClockAdapter(rusty::Arc<ManualRaftClock> clock)
      : clock_(std::move(clock)) {}

  uint64_t now_us() const override { return clock_->now_us(); }

 private:
  rusty::Arc<ManualRaftClock> clock_;
};

}  // namespace detail

inline RaftClockProxy make_system_raft_clock() {
  return RaftClockProxy(new SystemRaftClock());
}

inline RaftClockProxy make_manual_raft_clock(
    rusty::Arc<ManualRaftClock> clock) {
  return RaftClockProxy(new detail::ManualRaftClockAdapter(std::move(clock)));
}

}  // namespace raft
}  // namespace janus
