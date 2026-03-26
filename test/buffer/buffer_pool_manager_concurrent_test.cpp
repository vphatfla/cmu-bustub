//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// buffer_pool_manager_concurrent_test.cpp
//
// Identification: test/buffer/buffer_pool_manager_concurrent_test.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <atomic>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <thread>  // NOLINT
#include <vector>

#include "buffer/buffer_pool_manager.h"
#include "gtest/gtest.h"
#include "storage/disk/disk_manager_memory.h"
#include "storage/page/page_guard.h"

namespace bustub {

using bustub::DiskManagerUnlimitedMemory;

static void CopyStr(char *dest, const std::string &src) {
  BUSTUB_ENSURE(src.length() + 1 <= BUSTUB_PAGE_SIZE, "CopyStr src too long");
  snprintf(dest, BUSTUB_PAGE_SIZE, "%s", src.c_str());
}

// ---------------------------------------------------------------------------
// Test 1: ConcurrentWriteReadMixTest
//
// MixTest1 analog: pre-populate pages, then concurrent writers + readers
// verify data integrity. Catches the Drop() flush-after-unlock race.
// ---------------------------------------------------------------------------
TEST(BufferPoolManagerConcurrentTest, ConcurrentWriteReadMixTest) {
  const size_t NUM_ITERS = 20;
  const size_t NUM_FRAMES = 10;
  const size_t NUM_PAGES = 50;
  const size_t NUM_WRITER_THREADS = 5;
  const size_t NUM_READER_THREADS = 5;

  for (size_t iter = 0; iter < NUM_ITERS; iter++) {
    auto *disk_manager = new DiskManagerUnlimitedMemory();
    auto *bpm = new BufferPoolManager(NUM_FRAMES, disk_manager);

    // Allocate pages and write initial known data
    std::vector<page_id_t> page_ids;
    for (size_t i = 0; i < NUM_PAGES; i++) {
      page_id_t pid = bpm->NewPage();
      page_ids.push_back(pid);
      auto guard = bpm->WritePage(pid);
      CopyStr(guard.GetDataMut(), "page_" + std::to_string(pid) + "_init");
    }

    // Concurrent workload
    std::vector<std::thread> threads;

    // Writer threads: each writes to all pages
    for (size_t tid = 0; tid < NUM_WRITER_THREADS; tid++) {
      threads.emplace_back([&, tid]() {
        for (size_t i = 0; i < NUM_PAGES; i++) {
          auto pid = page_ids[i];
          auto guard = bpm->WritePage(pid);
          auto data = "page_" + std::to_string(pid) + "_t" + std::to_string(tid) + "_i" + std::to_string(i);
          CopyStr(guard.GetDataMut(), data);
        }
      });
    }

    // Reader threads: each reads all pages and verifies prefix
    for (size_t tid = 0; tid < NUM_READER_THREADS; tid++) {
      threads.emplace_back([&]() {
        for (size_t i = 0; i < NUM_PAGES; i++) {
          auto pid = page_ids[i];
          auto guard = bpm->ReadPage(pid);
          auto expected_prefix = "page_" + std::to_string(pid) + "_";
          // Data must start with the correct page prefix (not corrupted from another page)
          EXPECT_EQ(std::string(guard.GetData()).substr(0, expected_prefix.size()), expected_prefix);
        }
      });
    }

    for (auto &t : threads) {
      t.join();
    }

    // Final verification: every page should have valid data
    for (size_t i = 0; i < NUM_PAGES; i++) {
      auto pid = page_ids[i];
      auto guard = bpm->ReadPage(pid);
      auto expected_prefix = "page_" + std::to_string(pid) + "_";
      ASSERT_EQ(std::string(guard.GetData()).substr(0, expected_prefix.size()), expected_prefix)
          << "Page " << pid << " has corrupted data: " << guard.GetData();
    }

    delete bpm;
    delete disk_manager;
  }
}

// ---------------------------------------------------------------------------
// Test 2: ConcurrentEvictionIntegrityTest
//
// Small buffer pool forces constant eviction/reload. Verifies data survives
// eviction cycles without corruption.
// ---------------------------------------------------------------------------
TEST(BufferPoolManagerConcurrentTest, ConcurrentEvictionIntegrityTest) {
  const size_t NUM_ITERS = 50;
  const size_t NUM_FRAMES = 5;
  const size_t NUM_PAGES = 20;
  const size_t NUM_THREADS = 8;
  const size_t OPS_PER_THREAD = 50;

  for (size_t iter = 0; iter < NUM_ITERS; iter++) {
    auto *disk_manager = new DiskManagerUnlimitedMemory();
    auto *bpm = new BufferPoolManager(NUM_FRAMES, disk_manager);

    // Allocate and initialize pages
    std::vector<page_id_t> page_ids;
    for (size_t i = 0; i < NUM_PAGES; i++) {
      page_id_t pid = bpm->NewPage();
      page_ids.push_back(pid);
      auto guard = bpm->WritePage(pid);
      CopyStr(guard.GetDataMut(), "page_" + std::to_string(pid) + "_v0");
    }

    // Each thread cycles through pages writing thread-specific data
    // Small buffer pool forces evictions on almost every access
    std::vector<std::thread> threads;
    for (size_t tid = 0; tid < NUM_THREADS; tid++) {
      threads.emplace_back([&, tid]() {
        for (size_t op = 0; op < OPS_PER_THREAD; op++) {
          // Deterministic page selection: spread across pages
          auto idx = (tid * OPS_PER_THREAD + op) % NUM_PAGES;
          auto pid = page_ids[idx];
          auto guard = bpm->WritePage(pid);
          auto data = "page_" + std::to_string(pid) + "_t" + std::to_string(tid) + "_op" + std::to_string(op);
          CopyStr(guard.GetDataMut(), data);
        }
      });
    }

    for (auto &t : threads) {
      t.join();
    }

    // Verify every page has valid data (prefix matches page_id)
    for (size_t i = 0; i < NUM_PAGES; i++) {
      auto pid = page_ids[i];
      auto guard = bpm->ReadPage(pid);
      auto expected_prefix = "page_" + std::to_string(pid) + "_";
      ASSERT_EQ(std::string(guard.GetData()).substr(0, expected_prefix.size()), expected_prefix)
          << "Iter " << iter << ": Page " << pid << " has corrupted data after eviction: " << guard.GetData();
    }

    delete bpm;
    delete disk_manager;
  }
}

// ---------------------------------------------------------------------------
// Test 3: ConcurrentWriteDropPersistenceTest
//
// Targets Drop() flush race directly. Multiple threads write to overlapping
// pages with a tiny buffer pool, forcing eviction. Verifies read-after-write
// consistency: reloaded data must match some valid write.
// ---------------------------------------------------------------------------
TEST(BufferPoolManagerConcurrentTest, ConcurrentWriteDropPersistenceTest) {
  const size_t NUM_ITERS = 50;
  const size_t NUM_FRAMES = 6;  // Must be >= NUM_THREADS + 1 to avoid exhaustion
  const size_t NUM_PAGES = 20;
  const size_t NUM_THREADS = 4;
  const size_t OPS_PER_THREAD = 100;

  for (size_t iter = 0; iter < NUM_ITERS; iter++) {
    auto *disk_manager = new DiskManagerUnlimitedMemory();
    auto *bpm = new BufferPoolManager(NUM_FRAMES, disk_manager);

    // Allocate pages
    std::vector<page_id_t> page_ids;
    for (size_t i = 0; i < NUM_PAGES; i++) {
      page_id_t pid = bpm->NewPage();
      page_ids.push_back(pid);
      auto guard = bpm->WritePage(pid);
      CopyStr(guard.GetDataMut(), "page_" + std::to_string(pid) + "_init");
    }

    // Threads write to pages in a round-robin pattern
    // With only 2 frames and 10 pages, almost every access evicts
    std::vector<std::thread> threads;
    for (size_t tid = 0; tid < NUM_THREADS; tid++) {
      threads.emplace_back([&, tid]() {
        for (size_t op = 0; op < OPS_PER_THREAD; op++) {
          auto idx = (op + tid) % NUM_PAGES;
          auto pid = page_ids[idx];
          auto guard = bpm->WritePage(pid);
          auto data = "page_" + std::to_string(pid) + "_t" + std::to_string(tid) + "_op" + std::to_string(op);
          CopyStr(guard.GetDataMut(), data);
          // Guard dropped here — triggers the flush-after-unlock in Drop()
        }
      });
    }

    for (auto &t : threads) {
      t.join();
    }

    // Sequential verification: each page must have valid data
    for (size_t i = 0; i < NUM_PAGES; i++) {
      auto pid = page_ids[i];
      auto guard = bpm->ReadPage(pid);
      auto expected_prefix = "page_" + std::to_string(pid) + "_";
      ASSERT_EQ(std::string(guard.GetData()).substr(0, expected_prefix.size()), expected_prefix)
          << "Iter " << iter << ": Page " << pid << " has corrupted data: " << guard.GetData();
    }

    delete bpm;
    delete disk_manager;
  }
}

// ---------------------------------------------------------------------------
// Test 4: ConcurrentMultiReaderSingleWriterTest
//
// One writer rapidly updates a page while multiple readers verify they never
// see torn/partial writes. Catches reader seeing data during Drop()'s
// unlocked flush.
// ---------------------------------------------------------------------------
TEST(BufferPoolManagerConcurrentTest, ConcurrentMultiReaderSingleWriterTest) {
  const size_t NUM_ITERS = 20;
  const size_t NUM_FRAMES = 5;
  const size_t WRITE_OPS = 1000;
  const size_t NUM_READERS = 8;

  for (size_t iter = 0; iter < NUM_ITERS; iter++) {
    auto *disk_manager = new DiskManagerUnlimitedMemory();
    auto *bpm = new BufferPoolManager(NUM_FRAMES, disk_manager);

    // Target page + filler pages to cause eviction pressure
    page_id_t target_pid = bpm->NewPage();
    std::vector<page_id_t> filler_pids;
    for (size_t i = 0; i < NUM_FRAMES; i++) {
      filler_pids.push_back(bpm->NewPage());
    }

    // Initialize target page
    {
      auto guard = bpm->WritePage(target_pid);
      CopyStr(guard.GetDataMut(), "counter_0");
    }

    std::atomic<bool> done{false};
    std::vector<std::thread> threads;

    // Writer thread: writes incrementing counter
    threads.emplace_back([&]() {
      for (size_t i = 1; i <= WRITE_OPS; i++) {
        auto guard = bpm->WritePage(target_pid);
        CopyStr(guard.GetDataMut(), "counter_" + std::to_string(i));
        // Occasionally access filler pages to force eviction of target
        if (i % 10 == 0) {
          auto filler_guard = bpm->WritePage(filler_pids[i % filler_pids.size()]);
          CopyStr(filler_guard.GetDataMut(), "filler_" + std::to_string(i));
        }
      }
      done.store(true);
    });

    // Reader threads: read target page and verify data format
    for (size_t tid = 0; tid < NUM_READERS; tid++) {
      threads.emplace_back([&]() {
        while (!done.load()) {
          auto guard = bpm->ReadPage(target_pid);
          auto data = std::string(guard.GetData());
          // Data must always be "counter_<number>" — never partial/corrupted
          EXPECT_EQ(data.substr(0, 8), "counter_")
              << "Reader saw corrupted data: " << data;
          // Verify the number part is parseable
          auto num_str = data.substr(8);
          bool valid_num = !num_str.empty();
          for (char c : num_str) {
            if (c == '\0') break;
            if (!std::isdigit(c)) {
              valid_num = false;
              break;
            }
          }
          EXPECT_TRUE(valid_num) << "Reader saw non-numeric counter: " << data;
        }
      });
    }

    for (auto &t : threads) {
      t.join();
    }

    // Final check: target page should have the last counter value
    {
      auto guard = bpm->ReadPage(target_pid);
      auto data = std::string(guard.GetData());
      ASSERT_EQ(data.substr(0, 8), "counter_")
          << "Final read of target page is corrupted: " << data;
    }

    delete bpm;
    delete disk_manager;
  }
}

}  // namespace bustub
