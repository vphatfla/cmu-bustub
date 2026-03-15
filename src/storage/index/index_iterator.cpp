//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// index_iterator.cpp
//
// Identification: src/storage/index/index_iterator.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

/**
 * index_iterator.cpp
 */
#include <cassert>
#include <memory>
#include <optional>
#include <unordered_set>
#include <utility>
#include "buffer/buffer_pool_manager.h"
#include "common/config.h"
#include "common/macros.h"
#include "storage/page/b_plus_tree_page.h"

#include "storage/index/index_iterator.h"

namespace bustub {

/**
 * @note you can change the destructor/constructor method here
 * set your own input parameters
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE::~IndexIterator() = default;  // NOLINT

FULL_INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE::IndexIterator(std::shared_ptr<TracedBufferPoolManager> bpm, const KeyComparator &comparator,
                                  ReadPageGuard leaf_guard, const page_id_t page_id,
                                  const std::optional<KeyType> &key)
    : bpm_(std::move(bpm)), comparator_(comparator), page_id_(page_id) {
  if (page_id_ == INVALID_PAGE_ID) {
    key_index_ = 0;
    return;
  }

  read_guard_ = std::move(leaf_guard);
  leaf_page_ = read_guard_.As<LeafPage>();
  key_index_ = 0;

  if (key.has_value()) {
    auto left = 0;
    auto right = leaf_page_->GetSize();
    while (left < right) {
      auto mid = left + (right - left) / 2;
      auto cmp = comparator_(leaf_page_->KeyAt(mid), key.value());
      if (cmp < 0) {
        left = mid + 1;
      } else {
        right = mid;
      }
    }
    key_index_ = left;
  }

  tombstone_indices_set_.clear();
  auto indices = leaf_page_->GetIndexesInTombstones();
  tombstone_indices_set_ = {indices.begin(), indices.end()};

  FindAndSetValidIndex();

  if (key_index_ >= leaf_page_->GetSize()) {
    LoadPageAndIterator(leaf_page_->GetNextPageId(), key);
  }
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void INDEXITERATOR_TYPE::FindAndSetValidIndex() {
  while (key_index_ < leaf_page_->GetSize()) {
    if (tombstone_indices_set_.count(key_index_) != 0) {
      key_index_ += 1;
    } else {
      break;
    }
  }
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void INDEXITERATOR_TYPE::LoadPageAndIterator(const page_id_t page_id, const std::optional<KeyType> &key) {
  page_id_ = page_id;
  if (page_id == INVALID_PAGE_ID) {
    key_index_ = 0;
    return;
  }

  while (true) {
    read_guard_ = bpm_->ReadPage(page_id_);
    leaf_page_ = read_guard_.As<LeafPage>();
    key_index_ = 0;

    if (key.has_value()) {
      auto left = 0;
      auto right = leaf_page_->GetSize();
      while (left < right) {
        auto mid = left + (right - left) / 2;
        auto cmp = comparator_(leaf_page_->KeyAt(mid), key.value());
        if (cmp < 0) {
          left = mid + 1;
        } else {
          right = mid;
        }
      }
      key_index_ = left;
    }

    tombstone_indices_set_.clear();
    auto indices = leaf_page_->GetIndexesInTombstones();
    tombstone_indices_set_ = {indices.begin(), indices.end()};

    FindAndSetValidIndex();

    if (key_index_ < leaf_page_->GetSize()) {
      break;  // found a valid entry
    }

    // all entries tombstoned, advance to next sibling
    page_id_ = leaf_page_->GetNextPageId();
    if (page_id_ == INVALID_PAGE_ID) {
      key_index_ = 0;
      return;  // reached end of tree
    }
  }
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::IsEnd() -> bool { return page_id_ == INVALID_PAGE_ID; }

FULL_INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::operator*() -> std::pair<const KeyType &, const ValueType &> {
  BUSTUB_ENSURE(!IsEnd(), "iterator is not value");

  return {leaf_page_->KeyAt(key_index_), leaf_page_->ValueAt(key_index_)};
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::operator++() -> INDEXITERATOR_TYPE & {
  key_index_ += 1;
  FindAndSetValidIndex();
  if (key_index_ >= leaf_page_->GetSize()) {
    LoadPageAndIterator(leaf_page_->GetNextPageId(), std::nullopt);
  }
  return *this;
}

template class IndexIterator<GenericKey<4>, RID, GenericComparator<4>>;

template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>>;
template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>, 3>;
template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>, 2>;
template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>, 1>;
template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>, -1>;

template class IndexIterator<GenericKey<16>, RID, GenericComparator<16>>;

template class IndexIterator<GenericKey<32>, RID, GenericComparator<32>>;

template class IndexIterator<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub
