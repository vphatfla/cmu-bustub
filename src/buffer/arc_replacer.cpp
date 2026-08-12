// :bustub-keep-private:
//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// arc_replacer.cpp
//
// Identification: src/buffer/arc_replacer.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/arc_replacer.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include "common/config.h"
#include "common/exception.h"

namespace bustub {

ArcReplacer::ArcReplacer(size_t num_frames) : replacer_size_(num_frames) { mru_target_size_ = static_cast<size_t>(0); }

void ArcReplacer::PushFrontOfList(std::list<frame_id_t> &list, FrameStatus &f_status) {
  list.push_front(f_status.frame_id_);
  f_status.list_it_ = list.begin();
}

void ArcReplacer::PushFrontOfGhostList(std::list<page_id_t> &ghost_list, FrameStatus &f_status) {
  ghost_list.push_front(f_status.page_id_);
  f_status.list_it_ = ghost_list.begin();
}

auto ArcReplacer::EvictFromList(std::list<frame_id_t> &list, std::list<page_id_t> &ghost_list, ArcStatus new_arc_status)
    -> std::optional<frame_id_t> {
  for (auto it = list.rbegin(); it != list.rend(); ++it) {
    frame_id_t frame_id = *it;

    auto f_status = alive_map_[frame_id];
    if (f_status->evictable_) {
      // remove from alive list
      list.erase(f_status->list_it_);
      alive_map_.erase(frame_id);
      PushFrontOfGhostList(ghost_list, *f_status);
      f_status->arc_status_ = new_arc_status;
      ghost_map_[f_status->page_id_] = f_status;
      return frame_id;
    }
  }
  return std::nullopt;
}

auto ArcReplacer::Evict() -> std::optional<frame_id_t> {
  // If mru_.size < target, evict from mfu_, if can't evict from mru_
  // else, evict from mru_, if can't evict from mfu_
  std::lock_guard<std::mutex> lock(latch_);
  std::optional<frame_id_t> res;
  if (mru_.size() < mru_target_size_) {
    res = EvictFromList(mfu_, mfu_ghost_, ArcStatus::MFU_GHOST);
    if (!res.has_value()) {
      res = EvictFromList(mru_, mru_ghost_, ArcStatus::MRU_GHOST);
    }
  } else {
    res = EvictFromList(mru_, mru_ghost_, ArcStatus::MRU_GHOST);
    if (!res.has_value()) {
      res = EvictFromList(mfu_, mfu_ghost_, ArcStatus::MFU_GHOST);
    }
  }

  if (res.has_value()) {
    curr_size_ -= 1;
  }
  return res;
}

void ArcReplacer::RecordAccess(frame_id_t frame_id, page_id_t page_id, [[maybe_unused]] AccessType access_type) {
  std::lock_guard<std::mutex> lock(latch_);
  // 1. cache HIT: if page exits in MRU OR MFU
  // move page to front of MFU
  // rational: page is accessed again, then move to front of mfu_ to get the protection
  if (auto it = alive_map_.find(frame_id); it != alive_map_.end()) {
    auto f_status = it->second;
    if (f_status->arc_status_ == ArcStatus::MRU) {
      // found in mru_, move to mfu_ and remove from mru_
      auto list_it = f_status->list_it_;
      mru_.erase(list_it);

      PushFrontOfList(mfu_, *f_status);
      f_status->arc_status_ = ArcStatus::MFU;
    }
    return;
  }
  // 2. cache MISS: page exists in mru_ghost_
  // --- IF mru_ghost_.size >= mfu_ghost_.size -> mru_target_size_  += 1
  // --- ELSE mru_target_size_ = MIN(replacer_size_ OR mru_target_size_ + round_down(mfu_ghost_.size/mru_ghost_.size)
  // Move page to front of mfu_
  // Rational: if mru_ is little larger, DBMS could have had a cache hits
  if (auto it = ghost_map_.find(page_id); it != ghost_map_.end()) {
    auto f_status = it->second;

    if (f_status->arc_status_ == ArcStatus::MRU_GHOST) {
      // found in mru_ghost_, increase the mru_target_size_, move to front of mfu_
      if (mru_ghost_.size() >= mfu_ghost_.size()) {
        mru_target_size_ += 1;
      } else if (mru_ghost_.empty()) {
        // mfu_ghost_ / mru_ghost_ would divide by zero; an empty mru_ghost_ against a non-empty
        // mfu_ghost_ is the most lopsided case possible, so saturate the target (clamped below).
        mru_target_size_ = replacer_size_;
      } else {
        auto delta = std::floor(static_cast<double>(mfu_ghost_.size()) / mru_ghost_.size());
        mru_target_size_ += delta;
      }
      mru_target_size_ = std::min(mru_target_size_, replacer_size_);
      mru_ghost_.erase(f_status->list_it_);
    } else if (f_status->arc_status_ == ArcStatus::MFU_GHOST) {
      // found in mfu_ghost_, decrease mru_target_size_, move to front of mfu_
      if (mfu_ghost_.size() >= mru_ghost_.size()) {
        mru_target_size_ -= 1;
      } else if (mfu_ghost_.empty()) {
        // mru_ghost_ / mfu_ghost_ would divide by zero; an empty mfu_ghost_ against a non-empty
        // mru_ghost_ is the most lopsided case possible, so saturate the target down to zero.
        mru_target_size_ = 0;
      } else {
        auto delta = std::floor(static_cast<double>(mru_ghost_.size()) / mfu_ghost_.size());
        if (mru_target_size_ < delta) {
          mru_target_size_ = 0;
        } else {
          mru_target_size_ -= delta;
        }
      }
      mfu_ghost_.erase(f_status->list_it_);
    }
    ghost_map_.erase(page_id);

    f_status->frame_id_ = frame_id;
    PushFrontOfList(mfu_, *f_status);
    f_status->arc_status_ = ArcStatus::MFU;

    alive_map_[frame_id] = f_status;
    curr_size_ += 1;
    return;
  }

  // 3. cache MISS: page exists in mfu_ghost_
  // --- IF mfu_ghost_.size >= mru_ghost_.size -> mru_target_size_ -=1
  // --- ELSE mru_target_size_ = MAX(0 OR mru_target_size_ - round_down(mru_ghost_.size/mfu_ghost_.size)
  // Move page to front of mfu_
  // Rational: if mfu_ is little larger, DBMS could have had a cache hits

  // 4. cache MISS, ghost lists MISS
  // --- IF mru_.size + mru_ghost_.size = replacer_size_ --> kill last in mru_ghost_, then add page to front of mru_
  // --- ELSE (mru_.size + mru_ghost_.size) < replacer_size_ (ALWAYS)
  // ------ IF mru_.size + mru_ghost_.size + mfu_.size + mfu_ghost_.size = 2 * replacer_size_ --> kill last ir
  // mfu_ghost_ AND add page to the front of mru_
  // ------ ELSE --> add page to front of MRU

  if (mru_.size() + mru_ghost_.size() == replacer_size_) {
    // kill last in mru_ghost_, push to front of mru_
    auto ghost_page_id = mru_ghost_.back();
    ghost_map_.erase(ghost_page_id);
    mru_ghost_.pop_back();
  } else if (mru_.size() + mru_ghost_.size() + mfu_.size() + mfu_ghost_.size() == 2 * replacer_size_) {
    // kill last in mfu_ghost_, then add page to the front of mru_
    auto ghost_page_id = mfu_ghost_.back();
    ghost_map_.erase(ghost_page_id);
    mfu_ghost_.pop_back();
  }

  auto f_status = FrameStatus(page_id, frame_id, true, ArcStatus::MRU);
  PushFrontOfList(mru_, f_status);

  alive_map_[frame_id] = std::make_shared<FrameStatus>(f_status);
  curr_size_ += 1;
}

void ArcReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  std::lock_guard<std::mutex> lock(latch_);
  auto it = alive_map_.find(frame_id);
  if (it == alive_map_.end()) {
    // Frame not found - this can happen in race conditions, silently return
    return;
  }
  auto f_status = it->second;
  if (!set_evictable && f_status->evictable_) {
    curr_size_ -= 1;
  } else if (set_evictable && !f_status->evictable_) {
    curr_size_ += 1;
  }
  f_status->evictable_ = set_evictable;
}

void ArcReplacer::Remove(frame_id_t frame_id) {
  std::lock_guard<std::mutex> lock(latch_);
  if (auto it = alive_map_.find(frame_id); it != alive_map_.end()) {
    auto f_status = it->second;
    if (!f_status->evictable_) {
      throw new Exception("Frame can not be evicted, frame_id = {}");
      return;
    }
    if (f_status->arc_status_ == ArcStatus::MRU) {
      mru_.erase(f_status->list_it_);
    } else if (f_status->arc_status_ == ArcStatus::MFU) {
      mfu_.erase(f_status->list_it_);
    } else {
      throw new Exception("Alive frame does not have valid ArcStatus, frame ID = {}");
    }
    alive_map_.erase(frame_id);
    curr_size_ -= 1;
  }
}

auto ArcReplacer::Size() -> size_t { return curr_size_; }

}  // namespace bustub
