//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// index_iterator.h
//
// Identification: src/include/storage/index/index_iterator.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

/**
 * index_iterator.h
 * For range scan of b+ tree
 */
#pragma once
#include <memory>
#include <optional>
#include <unordered_set>
#include <utility>
#include "buffer/buffer_pool_manager.h"
#include "buffer/traced_buffer_pool_manager.h"
#include "common/config.h"
#include "common/macros.h"
#include "storage/page/b_plus_tree_leaf_page.h"
#include "storage/page/page_guard.h"

namespace bustub {

#define INDEXITERATOR_TYPE IndexIterator<KeyType, ValueType, KeyComparator, NumTombs>
#define SHORT_INDEXITERATOR_TYPE IndexIterator<KeyType, ValueType, KeyComparator>

FULL_INDEX_TEMPLATE_ARGUMENTS_DEFN
class IndexIterator {
  using LeafPage = BPlusTreeLeafPage<KeyType, ValueType, KeyComparator, NumTombs>;

 public:
  // you may define your own constructor based on your member variables
  ~IndexIterator();  // NOLINT

  IndexIterator(std::shared_ptr<TracedBufferPoolManager> bpm, const KeyComparator &comparator,
                ReadPageGuard leaf_guard, page_id_t page_id, const std::optional<KeyType> &key);

  auto IsEnd() -> bool;

  auto operator*() -> std::pair<const KeyType &, const ValueType &>;

  auto operator++() -> IndexIterator &;

  auto operator==(const IndexIterator &itr) const -> bool {
    return this->key_index_ == itr.key_index_ && this->page_id_ == itr.page_id_;
  }

  auto operator!=(const IndexIterator &itr) const -> bool {
    return !(*this == itr);  // negate the == above
  }

 private:
  // add your own private member variables here

  /// @brief BufferPoolManager shared pointer with the BPLusTree
  std::shared_ptr<TracedBufferPoolManager> bpm_;

  // @brief Key comparator
  [[maybe_unused]] KeyComparator comparator_;
  /// @brief ReadPageGuard of the current_page, might be null
  ReadPageGuard read_guard_;

  /// @brief Pointer leaf_page_
  const LeafPage *leaf_page_;

  /// @brief page_id_ of the current_page, could have INVALID_PAGE_ID
  page_id_t page_id_{INVALID_PAGE_ID};

  /// @brief unordered_set for quick look up if index is tombstoned
  std::unordered_set<size_t> tombstone_indices_set_;

  /// @brief index of this iterator k-v in the leaf page
  int key_index_{0};

  /// @brief find and set the valid index that is not tombstone >= key_index_
  void FindAndSetValidIndex();

  /// @brief load the iterator params
  /// this is needed since we might have to  "jump" to the new page
  void LoadPageAndIterator(page_id_t page_id, const std::optional<KeyType> &key);
};

}  // namespace bustub
