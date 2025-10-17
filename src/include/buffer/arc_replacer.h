#pragma once

#include <cstddef>
#include <list>
#include <memory>
#include <optional>
#include <unordered_map>
#include "common/config.h"
#include "common/macros.h"
namespace bustub {

enum class AccessType { Unknown = 0, Lookup, Scan, Index };

enum class ArcStatus { MRU, MFU, MRU_GHOST, MFU_GHOST };

struct FrameStatus {
  page_id_t page_id_;
  frame_id_t frame_id_;
  bool evictable_;
  ArcStatus arc_status_;
  FrameStatus(page_id_t pid, frame_id_t fid, bool ev, ArcStatus st)
      : page_id_(pid), frame_id_(fid), evictable_(ev), arc_status_(st) {}
};

class ArcReplacer {
 public:
  explicit ArcReplacer(size_t num_frames);

  DISALLOW_COPY_AND_MOVE(ArcReplacer);

  ~ArcReplacer() = default;

  // @brief Evict a frame, if no evictable frames, return std::nullopt
  auto Evict() -> std::optional<frame_id_t>;

  // @brief record page has been accessed at current ts, in given frame
  // be called after a page has been pinned to a frame
  void RecordAccess(frame_id_t frame_id, page_id_t page_id, AccessType access_type = AccessType::Unknown);

  // @brief Control whether a frame is evictable or not
  // Also control ArcReplace'size. When the pin count of a page = 0, its corresponding frame must be marked evictable
  void SetEvictable(frame_id_t frame_id, bool set_evictable);

  // @brief Remove a frame and its page from the replaceer if exists and is evictable
  // Only be called when a page is deleted in the BufferPoolManager
  void Remove(frame_id_t frame_id);

  // @brief Return the number of eictable frames that currently in the ArcReplacer
  auto Size() -> size_t;

 private:
  // TODO
  std::list<frame_id_t> mru_;
  std::list<frame_id_t> mfu_;
  std::list<page_id_t> mru_ghost_;
  std::list<page_id_t> mfu_ghost_;

  // record entry in mru_ and mfu_
  std::unordered_map<frame_id_t, std::shared_ptr<FrameStatus>> alive_map_;
  // record entry in mru_ghost_ and mfu_ghost_
  std::unordered_map<page_id_t, std::shared_ptr<FrameStatus>> ghost_map_;

  size_t curr_size_{0};
  size_t mru_target_size_{0};
  size_t replacer_size_;

  std::mutex latch_;
};
}  // namespace bustub
