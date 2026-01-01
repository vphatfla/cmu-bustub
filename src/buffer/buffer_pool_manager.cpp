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
#include <memory>
#include <mutex>
#include <optional>
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
auto BufferPoolManager::NewPage() -> page_id_t {
  /* std::lock_guard<std::mutex> lock(*bpm_latch_);
  auto next_page = next_page_id_ ++; */

  return next_page_id_++;
}

/**
 * @brief Removes a page from the database, both on disk and in memory.
 *
 * If the page is pinned in the buffer pool, this function does nothing and returns `false`. Otherwise, this function
 * removes the page from both disk and memory (if it is still in the buffer pool), returning `true`.
 *
 */
auto BufferPoolManager::DeletePage(page_id_t page_id) -> bool {
  if (auto pc = GetPinCount(page_id); pc.has_value() && pc.value() > 0) {
    return false;
  }

  std::lock_guard<std::mutex> lock(*bpm_latch_);
  if (auto it = page_table_.find(page_id); it != page_table_.end()) {
    auto frame = GetFrameHeaderByID(it->second);
    if (frame->is_loading_) {
      return false;
    }
    replacer_->SetEvictable(frame->frame_id_, true);
    replacer_->Remove(frame->frame_id_);
    frame->Reset();

    page_table_.erase(page_id);
    free_frames_.emplace_back(frame->frame_id_);
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
 */

auto BufferPoolManager::CheckedWritePage(page_id_t page_id, AccessType access_type) -> std::optional<WritePageGuard> {
  FrameSource frame_source;
  while (true) {
    std::shared_ptr<FrameHeader> frame;

    {
      std::lock_guard<std::mutex> bpm_lock(*bpm_latch_);

      if (auto it = page_table_.find(page_id); it != page_table_.end()) {
        frame = GetFrameHeaderByID(it->second);
        frame_source = FrameSource::HIT;
      } else if (auto fid = GetFreeFrameID(); fid.has_value()) {
        frame = GetFrameHeaderByID(fid.value());
        frame_source = FrameSource::MISS_FREE;
      } else if (auto eid = replacer_->Evict(); eid.has_value()) {
        frame = GetFrameHeaderByID(eid.value());
        frame_source = FrameSource::MISS_EVICTED;
      } else {
        return std::nullopt;
      }
    }

    frame->rwlatch_.lock();
    frame->pin_count_ += 1;

    {
      std::lock_guard<std::mutex> bpm_lock(*bpm_latch_);

      if (frame_source == FrameSource::HIT) {
        if (!frame->page_id_.has_value() || frame->page_id_.value() != page_id || frame->is_loading_.load()) {
          frame->pin_count_ -= 1;
          frame->rwlatch_.unlock();
          continue;
        }
      } else {
        if (page_table_.find(page_id) != page_table_.end()) {
          // two cache misses in parallel, need to re-check if other thread have already acquire the write guard
          frame->pin_count_ -= 1;
          frame->rwlatch_.unlock();
          frame->Reset();
          free_frames_.emplace_back(frame->frame_id_);
          continue;
        }
        if (frame->page_id_.has_value()) {
          page_table_.erase(frame->page_id_.value());
        }
        page_table_.insert({page_id, frame->frame_id_});
        frame->page_id_ = std::make_optional(page_id);
      }

      replacer_->RecordAcessAndSetEvictable(frame->frame_id_, frame->page_id_.value(), false);
    }

    if (frame_source != FrameSource::HIT) {
      auto future = ScheduleIO(*frame, false, page_id);
      BUSTUB_ASSERT(future.get(), "ScheduleIO must return true");
      frame->is_loading_.store(false);
    }

    return WritePageGuard(page_id, frame, replacer_, bpm_latch_, disk_scheduler_, bpm_cv_);
  }
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
  FrameSource frame_source;
  while (true) {
    std::shared_ptr<FrameHeader> frame;

    {
      std::lock_guard<std::mutex> bpm_lock(*bpm_latch_);

      if (auto it = page_table_.find(page_id); it != page_table_.end()) {
        frame = GetFrameHeaderByID(it->second);
        frame_source = FrameSource::HIT;
      } else if (auto fid = GetFreeFrameID(); fid.has_value()) {
        frame = GetFrameHeaderByID(fid.value());
        frame_source = FrameSource::MISS_FREE;
      } else if (auto eid = replacer_->Evict(); eid.has_value()) {
        frame = GetFrameHeaderByID(eid.value());
        frame_source = FrameSource::MISS_EVICTED;
      } else {
        return std::nullopt;
      }

    }

    frame->rwlatch_.lock_shared();
    frame->pin_count_ += 1;
    {
      std::lock_guard<std::mutex> bpm_lock(*bpm_latch_);

      if (frame_source == FrameSource::HIT) {
        if (!frame->page_id_.has_value() || frame->page_id_.value() != page_id || frame->is_loading_.load()) {
            frame->pin_count_ -= 1;
          frame->rwlatch_.unlock_shared();
          continue;
        }
      } else {
        if (page_table_.find(page_id) != page_table_.end()) {
          // two cache misses in parallel, need to re-check if other thread have already acquire the read guard to avoid
          // having too diff frame for same page
          frame->pin_count_ -= 1;
          frame->rwlatch_.unlock_shared();
          frame->Reset();
          free_frames_.emplace_back(frame->frame_id_);
          continue;
        }
        if (frame->page_id_.has_value()) {
          page_table_.erase(frame->page_id_.value());
        }
        page_table_.insert({page_id, frame->frame_id_});
        frame->page_id_ = std::make_optional(page_id);
        frame->is_loading_.store(true);
      }

      replacer_->RecordAcessAndSetEvictable(frame->frame_id_, frame->page_id_.value(), false);
    }

    // if the frame is a MISS, we must bring the data from disk
    if (frame_source != FrameSource::HIT) {
      auto future = ScheduleIO(*frame, false, page_id);
      BUSTUB_ASSERT(future.get(), "ScheduleIO must return true");
      frame->is_loading_.store(false);
    }

    return ReadPageGuard(page_id, frame, replacer_, bpm_latch_, disk_scheduler_, bpm_cv_);
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
    auto frame = GetFrameHeaderByID(it->second);
    if (!frame->is_dirty_) {
      return true;
    }

    frame->is_loading_ = true;
    auto future = ScheduleIO(*frame, true, page_id);
    BUSTUB_ASSERT(future.get(), "SCHEDULE IO must return TRUE");
    frame->is_loading_ = false;

    frame->is_dirty_ = false;
    return true;
  }
  return false;
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
  std::unique_lock<std::mutex> lock(*bpm_latch_);
  if (auto it = page_table_.find(page_id); it != page_table_.end()) {
    auto frame = GetFrameHeaderByID(it->second);
    if (!frame->is_dirty_) {
      return true;
    }

    frame->is_loading_ = true;
    auto future = ScheduleIO(*frame, true, page_id);
    lock.unlock();

    BUSTUB_ASSERT(future.get(), "Schedule IO must return TRUE");
    lock.lock();
    frame->is_loading_ = false;

    frame->is_dirty_ = false;
    return true;
  }
  return false;
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
  for (const auto &frame : frames_) {
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
  if (requests.empty()) {
    return;
  }
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
  for (const auto &frame : frames_) {
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
  if (requests.empty()) {
    return;
  }
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
  std::lock_guard<std::mutex> lock(*bpm_latch_);
  if (auto it = page_table_.find(page_id); it != page_table_.end()) {
    auto frame = GetFrameHeaderByID(it->second);
    return frame->pin_count_.load();
  }
  return std::nullopt;
}

auto BufferPoolManager::GetFreeFrameID() -> std::optional<frame_id_t> {
  if (free_frames_.empty()) {
    return std::nullopt;
  }

  auto res = free_frames_.back();
  free_frames_.pop_back();
  return res;
}

auto BufferPoolManager::GetFrameHeaderByID(frame_id_t fid) -> std::shared_ptr<FrameHeader> {
  if (static_cast<size_t>(fid) >= num_frames_) {
    throw std::runtime_error("Frame ID is not valid, can not be >= num_frames");
  }
  return frames_.at(fid);
}

// NOLINTNEXTLINE(readability-non-const-parameter)
auto BufferPoolManager::ScheduleIO(FrameHeader &frame, const bool &is_write, const page_id_t &page_id)
    -> std::future<bool> {
  std::promise<bool> promise;
  std::future<bool> future = promise.get_future();
  frame.is_loading_ = true;
  auto request = DiskRequest{
      .is_write_ = is_write, .data_ = frame.data_.data(), .page_id_ = page_id, .callback_ = std::move(promise)};
  auto requests = std::vector<DiskRequest>{};
  requests.emplace_back(std::move(request));
  disk_scheduler_->Schedule(requests);

  return future;
}
}  // namespace bustub
