//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// page_guard.cpp
//
// Identification: src/storage/page/page_guard.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "storage/page/page_guard.h"
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>
#include "buffer/arc_replacer.h"
#include "common/exception.h"
#include "common/macros.h"
#include "storage/disk/disk_scheduler.h"

namespace bustub {

ReadPageGuard::ReadPageGuard(page_id_t page_id, std::shared_ptr<FrameHeader> frame,
                             std::shared_ptr<ArcReplacer> replacer, std::shared_ptr<std::mutex> bpm_latch,
                             std::shared_ptr<DiskScheduler> disk_scheduler,
                             std::shared_ptr<std::condition_variable> bpm_cv_)
    : page_id_(page_id),
      frame_(std::move(frame)),
      replacer_(std::move(replacer)),
      bpm_latch_(std::move(bpm_latch)),
      disk_scheduler_(std::move(disk_scheduler)),
      bpm_cv_(std::move(bpm_cv_)) {
  frame_->page_id_ = page_id;
  frame_->is_write_ = false;
  frame_->pin_count_ += 1;

  replacer_->RecordAccess(frame_->frame_id_, frame_->page_id_.value());
  replacer_->SetEvictable(frame_->frame_id_, false);

  is_valid_ = true;
}

ReadPageGuard::ReadPageGuard(ReadPageGuard &&that) noexcept
    : page_id_(that.page_id_),
      frame_(std::move(that.frame_)),
      replacer_(std::move(that.replacer_)),
      bpm_latch_(std::move(that.bpm_latch_)),
      disk_scheduler_(std::move(that.disk_scheduler_)),
      is_valid_(that.is_valid_),
      bpm_cv_(std::move(that.bpm_cv_)) {
  that.is_valid_ = false;
}

auto ReadPageGuard::operator=(ReadPageGuard &&that) noexcept -> ReadPageGuard & {
  if (this != &that) {
    Drop();
    page_id_ = that.page_id_;
    frame_ = std::move(that.frame_);
    replacer_ = std::move(that.replacer_);
    bpm_latch_ = std::move(that.bpm_latch_);
    disk_scheduler_ = std::move(that.disk_scheduler_);
    is_valid_ = that.is_valid_;
    bpm_cv_ = std::move(that.bpm_cv_);
    that.is_valid_ = false;
  }
  return *this;
}

/**
 * @brief Gets the page ID of the page this guard is protecting.
 */
auto ReadPageGuard::GetPageId() const -> page_id_t {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid read guard");
  return page_id_;
}

/**
 * @brief Gets a `const` pointer to the page of data this guard is protecting.
 */
auto ReadPageGuard::GetData() const -> const char * {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid read guard");
  return frame_->GetData();
}

/**
 * @brief Returns whether the page is dirty (modified but not flushed to the disk).
 */
auto ReadPageGuard::IsDirty() const -> bool {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid read guard");
  return frame_->is_dirty_;
}

/**
 * @brief Flushes this page's data safely to disk.
 *
 * TODO(P1): Add implementation.
 */
void ReadPageGuard::Flush() {
  // use disk_scheduler_ to schedule the flush
  // acquire the lock
  std::unique_lock<std::mutex> lock(*bpm_latch_);
  if (!is_valid_ || !frame_->is_dirty_) {
    return;
  }
  std::promise<bool> p;
  std::future<bool> f = p.get_future();
  auto request = DiskRequest{
      .is_write_ = true,
      .data_ = frame_->GetDataMut(),
      .page_id_ = page_id_,
      .callback_ = std::move(p),
  };
  std::vector<DiskRequest> requests;
  requests.emplace_back(std::move(request));
  disk_scheduler_->Schedule(requests);
  lock.unlock();
  try {
    bool res = f.get();
    BUSTUB_ASSERT(res, "Result of flush must be TRUE");
    frame_->is_dirty_ = false;
  } catch (...) {
    throw Exception("Something went wrong when flush page to disk");
  }
}

void ReadPageGuard::Drop() {
  if (!is_valid_) {
    return;
  }
  std::lock_guard<std::mutex> lock(*bpm_latch_);
  frame_->pin_count_ -= 1;
  frame_->is_write_ = false;

  if (frame_->pin_count_ == 0) {
    replacer_->SetEvictable(frame_->frame_id_, true);
  }
  is_valid_ = false;
  bpm_cv_->notify_one();
}

/** @brief The destructor for `ReadPageGuard`. This destructor simply calls `Drop()`. */
ReadPageGuard::~ReadPageGuard() { Drop(); }

/**********************************************************************************************************************/
/**********************************************************************************************************************/
/**********************************************************************************************************************/

WritePageGuard::WritePageGuard(page_id_t page_id, std::shared_ptr<FrameHeader> frame,
                               std::shared_ptr<ArcReplacer> replacer, std::shared_ptr<std::mutex> bpm_latch,
                               std::shared_ptr<DiskScheduler> disk_scheduler,
                               std::shared_ptr<std::condition_variable> bpm_cv)
    : page_id_(page_id),
      frame_(std::move(frame)),
      replacer_(std::move(replacer)),
      bpm_latch_(std::move(bpm_latch)),
      disk_scheduler_(std::move(disk_scheduler)),
      bpm_cv_(std::move(bpm_cv)) {
  frame_->page_id_ = page_id;
  frame_->is_write_ = true;
  frame_->pin_count_ += 1;

  replacer_->RecordAccess(frame_->frame_id_, frame_->page_id_.value());
  replacer_->SetEvictable(frame_->frame_id_, false);

  is_valid_ = true;
}

// move constructor
WritePageGuard::WritePageGuard(WritePageGuard &&that) noexcept
    : page_id_(that.page_id_),
      frame_(std::move(that.frame_)),
      replacer_(std::move(that.replacer_)),
      bpm_latch_(std::move(that.bpm_latch_)),
      disk_scheduler_(std::move(that.disk_scheduler_)),
      is_valid_(that.is_valid_),
      bpm_cv_(std::move(that.bpm_cv_)) {
  that.is_valid_ = false;
}

auto WritePageGuard::operator=(WritePageGuard &&that) noexcept -> WritePageGuard & {
  if (this != &that) {
    Drop();
    page_id_ = that.page_id_;
    frame_ = std::move(that.frame_);
    replacer_ = std::move(that.replacer_);
    bpm_latch_ = std::move(that.bpm_latch_);
    disk_scheduler_ = std::move(that.disk_scheduler_);
    is_valid_ = that.is_valid_;
    bpm_cv_ = std::move(that.bpm_cv_);
    that.is_valid_ = false;
  }
  return *this;
}

/**
 * @brief Gets the page ID of the page this guard is protecting.
 */
auto WritePageGuard::GetPageId() const -> page_id_t {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid write guard");
  return page_id_;
}

/**
 * @brief Gets a `const` pointer to the page of data this guard is protecting.
 */
auto WritePageGuard::GetData() const -> const char * {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid write guard");
  return frame_->GetData();
}

/**
 * @brief Gets a mutable pointer to the page of data this guard is protecting.
 */
auto WritePageGuard::GetDataMut() -> char * {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid write guard");
  return frame_->GetDataMut();
}

/**
 * @brief Returns whether the page is dirty (modified but not flushed to the disk).
 */
auto WritePageGuard::IsDirty() const -> bool {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid write guard");
  return frame_->is_dirty_;
}

void WritePageGuard::Flush() {
  // use disk_scheduler_ to schedule the flush
  // acquire the lock
  std::unique_lock<std::mutex> lock(*bpm_latch_);
  if (!is_valid_ || !frame_->is_dirty_) {
    return;
  }
  std::promise<bool> p;
  std::future<bool> f = p.get_future();
  auto request = DiskRequest{
      .is_write_ = true,
      .data_ = frame_->GetDataMut(),
      .page_id_ = page_id_,
      .callback_ = std::move(p),
  };
  std::vector<DiskRequest> requests;
  requests.emplace_back(std::move(request));
  disk_scheduler_->Schedule(requests);
  lock.unlock();
  try {
    bool res = f.get();
    BUSTUB_ASSERT(res, "Result of flush must be TRUE");
    frame_->is_dirty_ = false;
  } catch (...) {
    throw Exception("Something went wrong when flush page to disk");
  }
}

void WritePageGuard::Drop() {
  if (!is_valid_) {
    return;
  }
  /* if (frame_->is_dirty_) {
    Flush();
  } */
  std::lock_guard<std::mutex> lock(*bpm_latch_);
  frame_->pin_count_ -= 1;
  frame_->is_write_ = false;

  if (frame_->pin_count_ == 0) {
    replacer_->SetEvictable(frame_->frame_id_, true);
  }
  is_valid_ = false;
  bpm_cv_->notify_one();
}

/** @brief The destructor for `WritePageGuard`. This destructor simply calls `Drop()`. */
WritePageGuard::~WritePageGuard() { Drop(); }

}  // namespace bustub
