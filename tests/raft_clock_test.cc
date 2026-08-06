#include <gtest/gtest.h>

#include <cstdint>
#include <thread>

#include <rusty/arc.hpp>

#include "deptran/raft/clock.hpp"

namespace janus::raft {
namespace {

TEST(RaftClockTest, SystemClockIsNonzeroAndMonotonic) {
  auto clock = make_system_raft_clock();
  const uint64_t first = clock->now_us();
  const uint64_t second = clock->now_us();

  EXPECT_NE(first, 0u);
  EXPECT_GE(second, first);
}

TEST(RaftClockTest, ManualClockStartsAtSuppliedEpochAndAdvances) {
  auto clock = rusty::Arc<ManualRaftClock>::make(41);

  EXPECT_EQ(clock->now_us(), 41u);
  EXPECT_EQ(clock->advance_by_us(9), 50u);
  EXPECT_EQ(clock->now_us(), 50u);
}

TEST(RaftClockTest, ManualClockProxySharesItsClock) {
  auto manual = rusty::Arc<ManualRaftClock>::make(100);
  auto proxy = make_manual_raft_clock(manual.clone());

  EXPECT_EQ(proxy->now_us(), 100u);
  EXPECT_EQ(manual->advance_by_us(23), 123u);
  EXPECT_EQ(proxy->now_us(), 123u);
}

TEST(RaftClockTest, ManualClockNeverMovesBackwardAcrossReaderAndAdvancer) {
  auto clock = rusty::Arc<ManualRaftClock>::make(0);
  auto reader_clock = clock.clone();
  bool moved_backward = false;

  std::thread reader([reader_clock, &moved_backward]() mutable {
    uint64_t previous = reader_clock->now_us();
    for (uint64_t i = 0; i < 10'000; ++i) {
      const uint64_t current = reader_clock->now_us();
      if (current < previous) {
        moved_backward = true;
        return;
      }
      previous = current;
    }
  });
  for (uint64_t i = 0; i < 10'000; ++i) {
    clock->advance_by_us(1);
  }
  reader.join();

  EXPECT_FALSE(moved_backward);
  EXPECT_EQ(clock->now_us(), 10'000u);
}

}  // namespace
}  // namespace janus::raft
