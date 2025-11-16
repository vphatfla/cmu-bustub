//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// buffer_pool_manager.cpp
//
// Identification: src/buffer/buffer_pool_manager.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <buffer/buffer_pool_manager.h>
#include <condition_variable>
#include <cstddef>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <utility>
#include <vector>
#include "buffer/arc_replacer.h"
#include "common/config.h"
#include "common/macros.h"
#include "storage/disk/disk_scheduler.h"
#include "storage/page/page_guard.h"

namespace bustub {

/**
 * @brief The constructor for a `FrameHeader` that initializes all fields to default values.
 *
 * See the documentation for `FrameHeader` in "buffer/buffer_pool_manager.h" for more information.
 *
 * @param frame_id The frame ID / index of the frame we are creating a header for.
 */
FrameHeader::FrameHeader(frame_id_t frame_id) : frame_id_(frame_id), data_(BUSTUB_PAGE_SIZE, 0) { Reset(); }

/**
 * @brief Get a raw const pointer to the frame's data.
 *
 * @return const char* A pointer to immutable data that the frame stores.
 */
auto FrameHeader::GetData() const -> const char * { return data_.data(); }

/**
 * @brief Get a raw mutable pointer to the frame's data.
 *
 * @return char* A pointer to mutable data that the frame stores.
 */
auto FrameHeader::GetDataMut() -> char * {
  is_dirty_ = true;  // return raw ptr that can be mutable, then set is_dirty =  true
  return data_.data();
}

/**
 * @brief Resets a `FrameHeader`'s member fields.
 */
void FrameHeader::Reset() {
  std::fill(data_.begin(), data_.end(), 0);
  pin_count_.store(0);
  is_dirty_ = false;
}

BufferPoolManager::BufferPoolManager(size_t num_frames, DiskManager *disk_manager, LogManager *log_manager)
    : num_frames_(num_frames),
      next_page_id_(0),
      bpm_latch_(std::make_shared<std::mutex>()),
      replacer_(std::make_shared<ArcReplacer>(num_frames)),
      disk_scheduler_(std::make_shared<DiskScheduler>(disk_manager)),
      bpm_cv_(std::make_shared<std::condition_variable>()),
      log_manager_(log_manager) {
  // Not strictly necessary...
  std::scoped_lock latch(*bpm_latch_);

  // Initialize the monotonically increasing counter at 0.
  next_page_id_.store(0);

  // Allocate all of the in-memory frames up front.
  frames_.reserve(num_frames_);

  // The page table should have exactly `num_frames_` slots, corresponding to exactly `num_frames_` frames.
  page_table_.reserve(num_frames_);

  // Initialize all of the frame headers, and fill the free frame list with all possible frame IDs (since all frames are
  // initially free).
  for (size_t i = 0; i < num_frames_; i++) {
    frames_.push_back(std::make_shared<FrameHeader>(i));
    free_frames_.push_back(static_cast<frame_id_t>(i));
  }
}

/**
 * @brief Destroys the `BufferPoolManager`, freeing up all memory that the buffer pool was using.
 */
BufferPoolManager::~BufferPoolManager() = default;

/**
 * @brief Returns the number of frames that this buffer pool manages.
 */
auto BufferPoolManager::Size() const -> size_t { return num_frames_; }

/**
 * @brief Allocates a new page on disk.
 *
 * ### Implementation
 *
 * You will maintain a thread-safe, monotonically increasing counter in the form of a `std::atomic<page_id_t>`.
 * See the documentation on [atomics](https://en.cppreference.com/w/cpp/atomic/atomic) for more information.
 *
 */
auto BufferPoolManager::NewPage() -> page_id_t { return next_page_id_++; }

/**
 * @brief Removes a page from the database, both on disk and in memory.
 *
 * If the page is pinned in the buffer pool, this function does nothing and returns `false`. Otherwise, this function
 * removes the page from both disk and memory (if it is still in the buffer pool), returning `true`.
 *
 * ### Implementation
 *
 * Think about all of the places that a page or a page's metadata could be, and use that to guide you on implementing
 * this function. You will probably want to implement this function _after_ you have implemented `CheckedReadPage` and
 * `CheckedWritePage`.
 *
 */
auto BufferPoolManager::DeletePage(page_id_t page_id) -> bool {
  // remove from memory: from bpm and replacer
  std::lock_guard<std::mutex> lock(*bpm_latch_);
  if (auto pc = GetPinCount(page_id); pc.has_value() && pc.value() > 0) {
    return false;
  }

  if (auto it = page_table_.find(page_id); it != page_table_.end()) {
    auto frame = getFrameHeaderByID(it->second);
    replacer_->SetEvictable(frame->frame_id_, true);
    replacer_->Remove(frame->frame_id_);
    frame->Reset();

    page_table_.erase(page_id);
  }
  // remove from disk: from diskschduler
  disk_scheduler_->DeallocatePage(page_id);
  return true;
}

/**
 * @brief Acquires an optional write-locked guard over a page of data. The user can specify an `AccessType` if needed.
 *
 * If it is not possible to bring the page of data into memory, this function will return a `std::nullopt`.
 *
 * Page data can _only_ be accessed via page guards. Users of this `BufferPoolManager` are expected to acquire either a
 * `ReadPageGuard` or a `WritePageGuard` depending on the mode in which they would like to access the data, which
 * ensures that any access of data is thread-safe.
 *
 * There can only be 1 `WritePageGuard` reading/writing a page at a time. This allows data access to be both immutable
 * and mutable, meaning the thread that owns the `WritePageGuard` is allowed to manipulate the page's data however they
 * want. If a user wants to have multiple threads reading the page at the same time, they must acquire a `ReadPageGuard`
 * with `CheckedReadPage` instead.
 *
 * ### Implementation
 *
 * There are three main cases that you will have to implement. The first two are relatively simple: one is when there is
 * plenty of available memory, and the other is when we don't actually need to perform any additional I/O. Think about
 * what exactly these two cases entail.
 *
 * The third case is the trickiest, and it is when we do not have any _easily_ available memory at our disposal. The
 * buffer pool is tasked with finding memory that it can use to bring in a page of memory, using the replacement
 * algorithm you implemented previously to find candidate frames for eviction.
 *
 * Once the buffer pool has identified a frame for eviction, several I/O operations may be necessary to bring in the
 * page of data we want into the frame.
 *
 */

auto BufferPoolManager::CheckedWritePage(page_id_t page_id, AccessType access_type) -> std::optional<WritePageGuard> {
  std::unique_lock<std::mutex> lock(*bpm_latch_);
  if (auto it = page_table_.find(page_id); it != page_table_.end()) {
    // cache HIT
    auto frame_header = getFrameHeaderByID(it->second);
    bpm_cv_->wait(lock, [&] { return frame_header->pin_count_ == 0; });
    if (frame_header->is_dirty_) {
      if (!FlushPageUnsafe(page_id)) {
        std::cout << "ERROR flushing page " << page_id << std::endl;
        return std::nullopt;
      }
    }

    return WritePageGuard(page_id, frame_header, replacer_, bpm_latch_, disk_scheduler_, bpm_cv_);
  } else {
    // cache MISSED
    auto free_frame_id = getFreeFrameID();
    if (free_frame_id.has_value()) {
      // available free frame
      auto free_frame = getFrameHeaderByID(free_frame_id.value());
      scheduleIO(false, free_frame->data_.data(), page_id);

      free_frame->page_id_ = std::make_optional(page_id);

      page_table_.insert({page_id, free_frame->frame_id_});

      return WritePageGuard(page_id, free_frame, replacer_, bpm_latch_, disk_scheduler_, bpm_cv_);
    } else {
      // no available free frame
      auto evicted_frame_id = replacer_->Evict();
      if (!evicted_frame_id.has_value()) {
        // no available free frame to evict
        return std::nullopt;
      }
      auto evicted_frame = getFrameHeaderByID(evicted_frame_id.value());
      if (evicted_frame->pin_count_ != 0) {
        throw std::runtime_error("evicted a frame is invalid since its pin count is not 0");
      }
      auto victim_page_id = evicted_frame->page_id_;
      if (!victim_page_id.has_value()) {
        throw std::runtime_error("page_id is NOT SET in the evicted frame");
      }

      if (evicted_frame->is_dirty_ && !FlushPageUnsafe(victim_page_id.value())) {
        throw std::runtime_error("failed to flush a page of evicted frame");
      }

      page_table_.erase(victim_page_id.value());
      scheduleIO(false, evicted_frame->data_.data(), page_id);
      evicted_frame->page_id_ = std::make_optional(page_id);
      page_table_.insert({evicted_frame->page_id_.value(), evicted_frame->frame_id_});
      return WritePageGuard(page_id, evicted_frame, replacer_, bpm_latch_, disk_scheduler_, bpm_cv_);
    }
  }

  //
  return std::nullopt;
};

/**
 * @brief Acquires an optional read-locked guard over a page of data. The user can specify an `AccessType` if needed.
 *
 * If it is not possible to bring the page of data into memory, this function will return a `std::nullopt`.
 *
 * Page data can _only_ be accessed via page guards. Users of this `BufferPoolManager` are expected to acquire either a
 * `ReadPageGuard` or a `WritePageGuard` depending on the mode in which they would like to access the data, which
 * ensures that any access of data is thread-safe.
 *
 * There can be any number of `ReadPageGuard`s reading the same page of data at a time across different threads.
 * However, all data access must be immutable. If a user wants to mutate the page's data, they must acquire a
 * `WritePageGuard` with `CheckedWritePage` instead.
 *
 */
auto BufferPoolManager::CheckedReadPage(page_id_t page_id, AccessType access_type) -> std::optional<ReadPageGuard> {
  std::unique_lock<std::mutex> lock(*bpm_latch_);
  if (auto it = page_table_.find(page_id); it != page_table_.end()) {
    // cache HIT
    auto frame_header = getFrameHeaderByID(it->second);
    bpm_cv_->wait(lock, [&] { return frame_header->pin_count_ == 0; });

    return ReadPageGuard(page_id, frame_header, replacer_, bpm_latch_, disk_scheduler_, bpm_cv_);
  } else {
    // cache MISSED
    if (auto free_frame_id = getFreeFrameID(); free_frame_id.has_value()) {
      // there is a free_frame
      auto free_frame = getFrameHeaderByID(free_frame_id.value());
      scheduleIO(false, free_frame->data_.data(), page_id);

      free_frame->page_id_ = std::make_optional(page_id);

      page_table_.insert({page_id, free_frame->frame_id_});

      return ReadPageGuard(page_id, free_frame, replacer_, bpm_latch_, disk_scheduler_, bpm_cv_);
    } else {
      // no free frame, need to evict
      auto evicted_frame_id = replacer_->Evict();
      if (!evicted_frame_id.has_value()) {
        return std::nullopt;
      }
      auto evicted_frame = getFrameHeaderByID(evicted_frame_id.value());
      if (evicted_frame->pin_count_ != 0) {
        throw std::runtime_error("evicted a frame is invalid since its pin count is not 0");
      }
      auto victim_page_id = evicted_frame->page_id_;
      if (!victim_page_id.has_value()) {
        throw std::runtime_error("page_id is NOT SET in the evicted frame");
      }

      if (evicted_frame->is_dirty_ && !FlushPageUnsafe(victim_page_id.value())) {
        throw std::runtime_error("failed to flush a page of evicted frame");
      }

      page_table_.erase(victim_page_id.value());

      scheduleIO(false, evicted_frame->data_.data(), page_id);
      evicted_frame->page_id_ = std::make_optional(page_id);

      page_table_.insert({evicted_frame->page_id_.value(), evicted_frame->frame_id_});

      return ReadPageGuard(page_id, evicted_frame, replacer_, bpm_latch_, disk_scheduler_, bpm_cv_);
    }
  }
};

/**
 * @brief A wrapper around `CheckedWritePage` that unwraps the inner value if it exists.
 *
 * If `CheckedWritePage` returns a `std::nullopt`, **this function aborts the entire process.**
 *
 * This function should **only** be used for testing and ergonomic's sake. If it is at all possible that the buffer pool
 * manager might run out of memory, then use `CheckedPageWrite` to allow you to handle that case.
 *
 * See the documentation for `CheckedPageWrite` for more information about implementation.
 *
 * @param page_id The ID of the page we want to read.
 * @param access_type The type of page access.
 * @return WritePageGuard A page guard ensuring exclusive and mutable access to a page's data.
 */
auto BufferPoolManager::WritePage(page_id_t page_id, AccessType access_type) -> WritePageGuard {
  auto guard_opt = CheckedWritePage(page_id, access_type);

  if (!guard_opt.has_value()) {
    fmt::println(stderr, "\n`CheckedWritePage` failed to bring in page {}\n", page_id);
    std::abort();
  }

  return std::move(guard_opt).value();
}

/**
 * @brief A wrapper around `CheckedReadPage` that unwraps the inner value if it exists.
 *
 * If `CheckedReadPage` returns a `std::nullopt`, **this function aborts the entire process.**
 *
 * This function should **only** be used for testing and ergonomic's sake. If it is at all possible that the buffer pool
 * manager might run out of memory, then use `CheckedPageWrite` to allow you to handle that case.
 *
 * See the documentation for `CheckedPageRead` for more information about implementation.
 *
 * @param page_id The ID of the page we want to read.
 * @param access_type The type of page access.
 * @return ReadPageGuard A page guard ensuring shared and read-only access to a page's data.
 */
auto BufferPoolManager::ReadPage(page_id_t page_id, AccessType access_type) -> ReadPageGuard {
  auto guard_opt = CheckedReadPage(page_id, access_type);

  if (!guard_opt.has_value()) {
    fmt::println(stderr, "\n`CheckedReadPage` failed to bring in page {}\n", page_id);
    std::abort();
  }

  return std::move(guard_opt).value();
}

/**
 * @brief Flushes a page's data out to disk unsafely.
 *
 * This function will write out a page's data to disk if it has been modified. If the given page is not in memory, this
 * function will return `false`.
 *
 * You should not take a lock on the page in this function.
 * This means that you should carefully consider when to toggle the `is_dirty_` bit.
 *
 * ### Implementation
 *
 * You should probably leave implementing this function until after you have completed `CheckedReadPage` and
 * `CheckedWritePage`, as it will likely be much easier to understand what to do.
 *
 */
auto BufferPoolManager::FlushPageUnsafe(page_id_t page_id) -> bool {
  if (auto it = page_table_.find(page_id); it != page_table_.end()) {
    auto frame = getFrameHeaderByID(it->second);
    if (!frame->is_dirty_) return true;
    scheduleIO(true, frame->data_.data(), page_id);
    frame->is_dirty_ = false;
    return true;
  } else {
    return false;
  }
}

/**
 * @brief Flushes a page's data out to disk safely.
 *
 * This function will write out a page's data to disk if it has been modified. If the given page is not in memory, this
 * function will return `false`.
 *
 * You should take a lock on the page in this function to ensure that a consistent state is flushed to disk.
 *
 * ### Implementation
 *
 * You should probably leave implementing this function until after you have completed `CheckedReadPage`,
 * `CheckedWritePage`, and `Flush` in the page guards, as it will likely be much easier to understand what to do.
 */
auto BufferPoolManager::FlushPage(page_id_t page_id) -> bool {
  std::lock_guard<std::mutex> lock(*bpm_latch_);
  if (auto it = page_table_.find(page_id); it != page_table_.end()) {
    auto frame = getFrameHeaderByID(it->second);
    if (!frame->is_dirty_) return true;
    scheduleIO(true, frame->data_.data(), page_id);
    frame->is_dirty_ = false;
    return true;
  } else {
    return false;
  }
}

/**
 * @brief Flushes all page data that is in memory to disk unsafely.
 *
 * You should not take locks on the pages in this function.
 * This means that you should carefully consider when to toggle the `is_dirty_` bit.
 *
 */
void BufferPoolManager::FlushAllPagesUnsafe() {
  auto requests = std::vector<DiskRequest>{};
  auto futures = std::vector<std::future<bool>>{};
  for (auto frame : frames_) {
    if (frame->is_dirty_) {
      BUSTUB_ASSERT(frame->page_id_.has_value(), "DIRTY frame must have a page id field");
      auto p = std::promise<bool>{};
      auto f = p.get_future();
      futures.emplace_back(std::move(f));
      auto r = DiskRequest{.is_write_ = true,
                           .data_ = frame->GetDataMut(),
                           .page_id_ = frame->page_id_.value(),
                           .callback_ = std::move(p)};
      requests.emplace_back(std::move(r));
      frame->is_dirty_ = false;
    }
  }
  if (requests.empty()) return;
  disk_scheduler_->Schedule(requests);
  for (auto &f : futures) {
    try {
      auto res = f.get();
      BUSTUB_ASSERT(res, "result of promise-future flush must be TRUE");
    } catch (...) {
      throw std::runtime_error("FLUSH error");
    }
  }
}

/**
 * @brief Flushes all page data that is in memory to disk safely.
 *
 * You should take locks on the pages in this function to ensure that a consistent state is flushed to disk.
 *
 */
void BufferPoolManager::FlushAllPages() {
  std::lock_guard<std::mutex> lock(*bpm_latch_);
  auto requests = std::vector<DiskRequest>{};
  auto futures = std::vector<std::future<bool>>{};
  for (auto frame : frames_) {
    if (frame->is_dirty_) {
      BUSTUB_ASSERT(frame->page_id_.has_value(), "DIRTY frame must have a page id field");
      auto p = std::promise<bool>{};
      auto f = p.get_future();
      futures.emplace_back(std::move(f));
      auto r = DiskRequest{.is_write_ = true,
                           .data_ = frame->GetDataMut(),
                           .page_id_ = frame->page_id_.value(),
                           .callback_ = std::move(p)};
      requests.emplace_back(std::move(r));
      frame->is_dirty_ = false;
    }
  }
  if (requests.empty()) return;
  disk_scheduler_->Schedule(requests);
  for (auto &f : futures) {
    try {
      auto res = f.get();
      BUSTUB_ASSERT(res, "result of promise-future flush must be TRUE");
    } catch (...) {
      throw std::runtime_error("FLUSH error");
    }
  }
}

/**
 * @brief Retrieves the pin count of a page. If the page does not exist in memory, return `std::nullopt`.
 *
 * This function is thread safe. Callers may invoke this function in a multi-threaded environment where multiple threads
 * access the same page.
 *
 * This function is intended for testing purposes. If this function is implemented incorrectly, it will definitely cause
 * problems with the test suite and autograder.
 *
 */
auto BufferPoolManager::GetPinCount(page_id_t page_id) -> std::optional<size_t> {
  if (auto it = page_table_.find(page_id); it != page_table_.end()) {
    auto frame = getFrameHeaderByID(it->second);
    return frame->pin_count_.load();
  } else {
    return std::nullopt;
  }
}

auto BufferPoolManager::getFreeFrameID() -> std::optional<frame_id_t> {
  if (free_frames_.empty()) {
    return std::nullopt;
  }

  auto res = free_frames_.back();
  free_frames_.pop_back();
  return res;
}

auto BufferPoolManager::getFrameHeaderByID(frame_id_t fid) -> std::shared_ptr<FrameHeader> {
  if (static_cast<size_t>(fid) >= num_frames_) {
    throw std::runtime_error("Frame ID is not valid, can not be >= num_frames");
  }
  return frames_.at(fid);
}

void BufferPoolManager::scheduleIO(const bool &is_write, char *data, const page_id_t &page_id) {
  std::promise<bool> promise;
  std::future<bool> future = promise.get_future();
  auto request =
      DiskRequest{.is_write_ = is_write, .data_ = data, .page_id_ = page_id, .callback_ = std::move(promise)};
  auto requests = std::vector<DiskRequest>{};
  requests.emplace_back(std::move(request));
  disk_scheduler_->Schedule(requests);
  auto res = future.get();

  BUSTUB_ASSERT(res, "result of IO request must be TRUE");
}
}  // namespace bustub
