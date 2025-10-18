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
#include <optional>
#include "common/config.h"
#include "common/exception.h"

namespace bustub {

/**
 *
 * TODO(P1): Add implementation
 *
 * @brief a new ArcReplacer, with lists initialized to be empty and target size to 0
 * @param num_frames the maximum number of frames the ArcReplacer will be required to cache
 */
ArcReplacer::ArcReplacer(size_t num_frames) : replacer_size_(num_frames) { mru_target_size_ = 0; }

auto ArcReplacer::evictFromList(std::list<frame_id_t> list, std::list<page_id_t> ghost_list, ArcStatus new_arc_status)
    -> std::optional<frame_id_t> {
  for (auto it = list.rbegin(); it != list.rend(); ++it) {
    frame_id_t frame_id = *it;

    auto f_status = alive_map_[frame_id];
    if (f_status->evictable_) {
      // remove from alive list
      list.erase(f_status->list_it_);
      alive_map_.erase(frame_id);
      // add to ghost list
      ghost_list.push_front(f_status->page_id_);
      f_status->evictable_ = false;
      f_status->arc_status_ = new_arc_status;
      f_status->list_it_ = ghost_list.begin();
      return frame_id;
    }
  }
  return std::nullopt;
}
/**
 * TODO(P1): Add implementation
 *
 * @brief Performs the Replace operation as described by the writeup
 * that evicts from either mfu_ or mru_ into its corresponding ghost list
 * according to balancing policy.
 *
 * If you wish to refer to the original ARC paper, please note that there are
 * two changes in our implementation:
 * 1. When the size of mru_ equals the target size, we don't check
 * the last access as the paper did when deciding which list to evict from.
 * This is fine since the original decision is stated to be arbitrary.
 * 2. Entries that are not evictable are skipped. If all entries from the desired side
 * (mru_ / mfu_) are pinned, we instead try victimize the other side (mfu_ / mru_),
 * and move it to its corresponding ghost list (mfu_ghost_ / mru_ghost_).
 *
 * @return frame id of the evicted frame, or std::nullopt if cannot evict
 */
auto ArcReplacer::Evict() -> std::optional<frame_id_t> {
  // If mru_.size < target, evict from mfu_, if can't evict from mru_
  // else, evict from mru_, if can't evict from mfu_
  std::optional<frame_id_t> res;
  if (mru_.size() < mru_target_size_) {
    res = evictFromList(mfu_, mfu_ghost_, ArcStatus::MFU_GHOST);
    if (!res.has_value()) {
      res = evictFromList(mru_, mru_ghost_, ArcStatus::MRU_GHOST);
    }
  } else {
    res = evictFromList(mru_, mru_ghost_, ArcStatus::MRU_GHOST);
    if (!res.has_value()) {
      res = evictFromList(mfu_, mfu_ghost_, ArcStatus::MFU_GHOST);
    }
  }
  return res;
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Record access to a frame, adjusting ARC bookkeeping accordingly
 * by bring the accessed page to the front of mfu_ if it exists in any of the lists
 * or the front of mru_ if it does not.
 *
 * Performs the operations EXCEPT REPLACE described in original paper, which is
 * handled by `Evict()`.
 *
 * Consider the following four cases, handle accordingly:
 * 1. Access hits mru_ or mfu_
 * 2/3. Access hits mru_ghost_ / mfu_ghost_
 * 4. Access misses all the lists
 *
 * This routine performs all changes to the four lists as preperation
 * for `Evict()` to simply find and evict a victim into ghost lists.
 *
 * Note that frame_id is used as identifier for alive pages and
 * page_id is used as identifier for the ghost pages, since page_id is
 * the unique identifier to the page after it's dead.
 * Using page_id for alive pages should be the same since it's one to one mapping,
 * but using frame_id is slightly more intuitive.
 *
 * @param frame_id id of frame that received a new access.
 * @param page_id id of page that is mapped to the frame.
 * @param access_type type of access that was received. This parameter is only needed for
 * leaderboard tests.
 */
void ArcReplacer::RecordAccess(frame_id_t frame_id, page_id_t page_id, [[maybe_unused]] AccessType access_type) {
  // 1. cache HIT: if page exits in MRU OR MFU
  // move page to front of MFU
  // rational: page is accessed again, then move to front of mfu_ to get the protection
  if (auto it = alive_map_.find(frame_id); it != alive_map_.end()) {
    auto f_status = it->second;
    if (f_status->arc_status_ == ArcStatus::MRU) {
      // found in mru_, move to mfu_ and remove from mru_
      auto list_it = f_status->list_it_;

      mfu_.push_front(frame_id);
      f_status->arc_status_ = ArcStatus::MFU;
      f_status->list_it_ = mfu_.begin();

      mru_.erase(list_it);
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
      } else {
        mru_target_size_ += std::floor(mfu_ghost_.size() / mru_ghost_.size());
      }
      mru_target_size_ = std::min(mru_target_size_, replacer_size_);
    } else if (f_status->arc_status_ == ArcStatus::MFU_GHOST) {
      // found in mfu_ghost_, decrease mru_target_size_, move to front of mfu_
      if (mfu_ghost_.size() >= mru_ghost_.size()) {
        mru_target_size_ -= 1;
      } else {
        mru_target_size_ -= std::floor(mru_ghost_.size() / mfu_ghost_.size());
      }
      mru_target_size_ = std::max(mru_target_size_, size_t(0));
    }

    f_status->frame_id_ = frame_id;
    f_status->arc_status_ = ArcStatus::MFU;
    mfu_.push_front(frame_id);
    f_status->list_it_ = mfu_.begin();

    alive_map_[frame_id] = f_status;

    ghost_map_.erase(page_id);
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
  // ------ IF mru_.size + mru_ghost_.size + mfu_.size + mfu_ghost_.size = 2 * replacer_size_ --> kill last in
  // mfu_ghost_ AND add page to the front of mru_
  // ------ ELSE --> add page to front of MRU

  if (mru_.size() + mru_ghost_.size() == replacer_size_) {
    // kill last in mru_ghost_, push to front of mru_
    auto ghost_page_id = mru_ghost_.back();
    ghost_map_.erase(ghost_page_id);
    mru_ghost_.pop_back();

    mru_.push_front(frame_id);
    auto f_status = FrameStatus(page_id, frame_id, true, ArcStatus::MRU, mru_.begin());
    alive_map_[frame_id] = std::make_shared<FrameStatus>(f_status);
    return;
  }

  if (mru_.size() + mru_ghost_.size() + mfu_.size() + mfu_ghost_.size() == 2 * replacer_size_) {
    // kill last in mfu_ghost_, then add page to the front of mru_
    auto ghost_page_id = mfu_ghost_.back();
    ghost_map_.erase(ghost_page_id);
    mfu_ghost_.pop_back();

    mru_.push_front(frame_id);
    auto f_status = FrameStatus(page_id, frame_id, true, ArcStatus::MRU, mru_.begin());
    alive_map_[frame_id] = std::make_shared<FrameStatus>(f_status);
    return;
  }

  mru_.push_front(frame_id);
  auto f_status = FrameStatus(page_id, frame_id, true, ArcStatus::MRU, mru_.begin());
  alive_map_[frame_id] = std::make_shared<FrameStatus>(f_status);
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Toggle whether a frame is evictable or non-evictable. This function also
 * controls replacer's size. Note that size is equal to number of evictable entries.
 *
 * If a frame was previously evictable and is to be set to non-evictable, then size should
 * decrement. If a frame was previously non-evictable and is to be set to evictable,
 * then size should increment.
 *
 * If frame id is invalid, throw an exception or abort the process.
 *
 * For other scenarios, this function should terminate without modifying anything.
 *
 * @param frame_id id of frame whose 'evictable' status will be modified
 * @param set_evictable whether the given frame is evictable or not
 */
void ArcReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  auto it = alive_map_.find(frame_id);
  if (it == alive_map_.end()) {
    throw new Exception("No alive frame id found for ", frame_id);
  }
  auto f_status = it->second;
  f_status->evictable_ = true;
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Remove an evictable frame from replacer.
 * This function should also decrement replacer's size if removal is successful.
 *
 * Note that this is different from evicting a frame, which always remove the frame
 * decided by the ARC algorithm.
 *
 * If Remove is called on a non-evictable frame, throw an exception or abort the
 * process.
 *
 * If specified frame is not found, directly return from this function.
 *
 * @param frame_id id of frame to be removed
 */
void ArcReplacer::Remove(frame_id_t frame_id) {
  if (auto it = alive_map_.find(frame_id); it != alive_map_.end()) {
    auto f_status = it->second;
    if (!f_status->evictable_) {
      throw new Exception("Frame can not be evicted, frame_id = ", frame_id);
      return;
    }
    if (f_status->arc_status_ == ArcStatus::MRU) {
      mru_.erase(f_status->list_it_);
    } else if (f_status->arc_status_ == ArcStatus::MFU) {
      mfu_.erase(f_status->list_it_);
    } else {
      throw new Exception("Alive frame does not have valid ArcStatus, frame ID = ", frame_id);
    }
    alive_map_.erase(frame_id);
  }
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Return replacer's size, which tracks the number of evictable frames.
 *
 * @return size_t
 */
auto ArcReplacer::Size() -> size_t { return curr_size_; }

}  // namespace bustub
