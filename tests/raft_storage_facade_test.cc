#include <gtest/gtest.h>

#include "deptran/raft/log_storage_facade.hpp"
#include "deptran/raft/memory_log_storage.hpp"
#include "deptran/raft/memory_snapshot_manager.hpp"
#include "deptran/raft/snapshot_manager_facade.hpp"

using namespace janus::raft;

TEST(LogStorageProxyTest, ForwardsEveryLogStorageMethod) {
  auto backend = std::make_shared<InMemoryLogStorage>();
  auto storage = make_log_storage_proxy(backend);

  ASSERT_TRUE(storage);
  EXPECT_EQ(storage.backend().get(), backend.get());
  EXPECT_TRUE(storage.empty());
  EXPECT_TRUE(storage.is_open());

  const auto first = LogEntry::with_slot_term(2, 7);
  const auto second = LogEntry::with_slot_term(4, 9);
  const auto third = LogEntry::with_slot_term(6, 11);
  ASSERT_TRUE(storage.put(first));
  ASSERT_TRUE(storage.put_batch({second, third}));

  auto found = storage.get(2);
  ASSERT_TRUE(found.is_some());
  EXPECT_EQ(found.unwrap().term, 7u);
  EXPECT_EQ(storage.get_first_index(), 2u);
  EXPECT_EQ(storage.get_last_index(), 6u);
  EXPECT_EQ(storage.size(), 3u);
  EXPECT_EQ(storage.get_range(2, 6).size(), 2u);

  auto term = storage.get_term(4);
  ASSERT_TRUE(term.is_some());
  EXPECT_EQ(term.unwrap(), 9);
  ASSERT_TRUE(storage.set_metadata("term", "9"));
  auto metadata = storage.get_metadata("term");
  ASSERT_TRUE(metadata.is_some());
  EXPECT_EQ(metadata.unwrap(), "9");
  EXPECT_TRUE(storage.sync());

  EXPECT_TRUE(storage.remove(2));
  EXPECT_TRUE(storage.remove_range(4, 7));
  EXPECT_TRUE(storage.empty());
  EXPECT_TRUE(storage.clear());
  EXPECT_TRUE(storage.close());
  EXPECT_FALSE(storage.is_open());
}

TEST(SnapshotManagerProxyTest, ForwardsEverySnapshotManagerMethod) {
  auto backend = std::make_shared<MemorySnapshotManager>();
  auto manager = make_snapshot_manager_proxy(backend);

  ASSERT_TRUE(manager);
  EXPECT_EQ(manager.backend().get(), backend.get());
  EXPECT_EQ(manager.GetStoragePath(), "<memory>");

  const std::string first_payload = "first snapshot";
  ASSERT_TRUE(manager.TakeSnapshot(8, 3, first_payload.data(),
                                   first_payload.size()));
  auto first = manager.GetLatestSnapshot();
  ASSERT_TRUE(first.is_some());
  EXPECT_EQ(first.unwrap().last_included_index, 8u);
  EXPECT_TRUE(manager.HasSnapshotAtOrAfter(8));
  EXPECT_FALSE(manager.HasSnapshotAtOrAfter(9));
  EXPECT_EQ(manager.ListSnapshots().size(), 1u);

  SnapshotMetadata loaded_metadata{};
  std::string loaded_payload;
  ASSERT_TRUE(manager.LoadLatestSnapshot(&loaded_metadata, &loaded_payload));
  EXPECT_EQ(loaded_metadata.last_included_term, 3u);
  EXPECT_EQ(loaded_payload, first_payload);

  auto writer = manager.BeginSnapshot(12, 5);
  ASSERT_NE(writer, nullptr);
  ASSERT_TRUE(writer->Write("next", 4));
  ASSERT_TRUE(writer->Finalize());
  auto latest = manager.GetLatestSnapshot();
  ASSERT_TRUE(latest.is_some());
  auto reader = manager.BeginLoad(latest.unwrap());
  ASSERT_NE(reader, nullptr);
  char buffer[8] = {};
  size_t bytes_read = 0;
  ASSERT_TRUE(reader->Read(buffer, sizeof(buffer), &bytes_read));
  EXPECT_EQ(std::string(buffer, bytes_read), "next");
  EXPECT_TRUE(reader->IsComplete());

  EXPECT_EQ(manager.PruneSnapshots(13), 1u);
  ASSERT_TRUE(manager.TakeSnapshot(14, 6, "last", 4));
  EXPECT_EQ(manager.DeleteAllSnapshots(), 1u);
  EXPECT_FALSE(manager.GetLatestSnapshot().is_some());
}
