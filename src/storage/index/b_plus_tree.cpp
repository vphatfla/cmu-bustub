//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree.cpp
//
// Identification: src/storage/index/b_plus_tree.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "storage/index/b_plus_tree.h"
#include <cstddef>
#include <utility>
#include "buffer/traced_buffer_pool_manager.h"
#include "common/config.h"
#include "storage/index/b_plus_tree_debug.h"
#include "storage/page/b_plus_tree_header_page.h"
#include "storage/page/b_plus_tree_leaf_page.h"
#include "storage/page/b_plus_tree_page.h"
#include "storage/page/page_guard.h"

namespace bustub {

FULL_INDEX_TEMPLATE_ARGUMENTS
BPLUSTREE_TYPE::BPlusTree(std::string name, page_id_t header_page_id, BufferPoolManager *buffer_pool_manager,
                          const KeyComparator &comparator, int leaf_max_size, int internal_max_size)
    : bpm_(std::make_shared<TracedBufferPoolManager>(buffer_pool_manager)),
      index_name_(std::move(name)),
      comparator_(std::move(comparator)),
      leaf_max_size_(leaf_max_size),
      internal_max_size_(internal_max_size),
      header_page_id_(header_page_id) {
  WritePageGuard guard = bpm_->WritePage(header_page_id_);
  auto root_page = guard.AsMut<BPlusTreeHeaderPage>();
  root_page->root_page_id_ = INVALID_PAGE_ID;
}

/**
 * @brief Helper function to decide whether current b+tree is empty
 * @return Returns true if this B+ tree has no keys and values.
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::IsEmpty() const -> bool {
  ReadPageGuard guard = bpm_->ReadPage(header_page_id_);
  auto root_page = guard.As<BPlusTreeHeaderPage>();
  return (root_page->root_page_id_ == INVALID_PAGE_ID);
}

/*****************************************************************************
 * SEARCH
 *****************************************************************************/
/**
 * @brief Return the only value that associated with input key
 *
 * This method is used for point query
 *
 * @param key input key
 * @param[out] result vector that stores the only value that associated with input key, if the value exists
 * @return : true means key exists
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetValue(const KeyType &key, std::vector<ValueType> *result) -> bool {
  // Declaration of context instance. Using the Context is not necessary but advised.
  Context ctx;

  // try to get and acquire read guard on the root page
  ReadPageGuard guard = bpm_->ReadPage(header_page_id_);
  auto header_page = guard.As<BPlusTreeHeaderPage>();
  if (header_page->root_page_id_ == INVALID_PAGE_ID) {
    return false;
  }

  ReadPageGuard root_guard = bpm_->ReadPage(header_page->root_page_id_);
  ctx.read_set_.emplace_back(std::move(root_guard));

  while (true) {
    // traverse internal pages
    auto temp_page = ctx.read_set_.back().As<BPlusTreePage>();
    if (temp_page->IsLeafPage()) {
      break;
    }
    auto curr_page = ctx.read_set_.back().As<InternalPage>();
    auto size = curr_page->GetSize();
    auto left = 1, right = size - 1;  // internal page index 0 is INVALID
    while (left <= right) {
      int mid = left + (right - left) / 2;
      if (comparator_(key, curr_page->KeyAt(mid)) >= 0) {
        left = mid + 1;
      } else {
        right = mid - 1;
      }
    }
    auto next_child_page_id = curr_page->ValueAt(right);
    ReadPageGuard curr_guard = bpm_->ReadPage(next_child_page_id);
    ctx.read_set_.emplace_back(std::move(curr_guard));
    ctx.read_set_.pop_front();
  }

  auto leaf_page = ctx.read_set_.back().As<LeafPage>();
  auto size = leaf_page->GetSize();
  auto left = 0, right = size - 1;

  while (left <= right) {
    auto mid = left + (right - left) / 2;
    auto key_mid = leaf_page->KeyAt(mid);
    auto cpm_result = comparator_(key, key_mid);
    if (cpm_result == 0) {
      // equal, add the value to result
      auto tombstones = leaf_page->GetTombstones();
      for (const auto &tt : tombstones) {
        if (comparator_(tt, key) == 0) {
          return false;
        }
      }

      result->emplace_back(leaf_page->RecordIDAt(mid));
      ctx.read_set_.pop_front();
      return true;
    } else if (cpm_result < 0) {
      // key < key_mid
      right = mid - 1;
      continue;
    } else {
      // key > key_mid
      left = mid + 1;
    }
  }

  ctx.read_set_.pop_front();
  return false;
}

/*****************************************************************************
 * INSERTION
 *****************************************************************************/
/**
 * @brief Insert constant key & value pair into b+ tree
 *
 * if current tree is empty, start new tree, update root page id and insert
 * entry; otherwise, insert into leaf page.
 *
 * @param key the key to insert
 * @param value the value associated with key
 * @return: since we only support unique key, if user try to insert duplicate
 * keys return false; otherwise, return true.
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Insert(const KeyType &key, const ValueType &value) -> bool {
  // Declaration of context instance. Using the Context is not necessary but advised.
  Context ctx;

  // root header page
  WritePageGuard guard = bpm_->WritePage(header_page_id_);
  auto header_page = guard.AsMut<BPlusTreeHeaderPage>();
  if (header_page->root_page_id_ == INVALID_PAGE_ID) {
    // tree is empty
    auto new_page_id = bpm_->NewPage();
    header_page->root_page_id_ = new_page_id;

    WritePageGuard leaf_guard = bpm_->WritePage(new_page_id);
    auto leaf_page = leaf_guard.AsMut<LeafPage>();
    leaf_page->Init();

    ctx.write_set_.emplace_back(std::move(leaf_guard));
  }

  // internal page traversal
    while (true) {
        auto temp_page = ctx.write_set_.back().As<BPlusTreePage>();
        if (temp_page->IsLeafPage()) {
          break;
        }
        auto curr_page = ctx.write_set_.back().As<InternalPage>();
        auto size = curr_page->GetSize();
        auto left = 1, right = size - 1;  // internal page index 0 is INVALID
        while (left <= right) {
          int mid = left + (right - left) / 2;
          if (comparator_(key, curr_page->KeyAt(mid)) >= 0) {
            left = mid + 1;
          } else {
            right = mid - 1;
          }
        }
        auto next_child_page_id = curr_page->ValueAt(right);
        WritePageGuard curr_guard = bpm_->WritePage(next_child_page_id);
        ctx.write_set_.emplace_back(std::move(curr_guard));
        // ctx.write_set_.pop_front(); // do not remove previous page write guard in case of splitting
    }

    auto leaf_guard = std::move(ctx.write_set_.back());
    auto leaf_page = leaf_guard.AsMut<LeafPage>();

    auto insertPos = static_cast<int>(FindInsertPosition(leaf_page, key));

    if (insertPos < leaf_page->GetSize()) {
        // release the parent guard
        DrainQueueUntilSize(ctx.write_set_, 1);
        if (comparator_(leaf_page->KeyAt(insertPos), key) == 0) {
            // key exists, might be deleted
            // check if tomstone
            if (leaf_page->IsIndexInTombstones(insertPos)) {
                leaf_page->SetKeyAt(insertPos, key);
                leaf_page->SetValueAt(insertPos, value);
                // remove from tombstone
                leaf_page->RemoveIndexFromTombstones(insertPos);
                DrainQueueUntilSize(ctx.write_set_, 0);
                return true;
            }
            // duplicate, not support, key must be unique
            DrainQueueUntilSize(ctx.write_set_, 0);
            return false;
        }
    }

    if (leaf_page->GetSize() < leaf_page->GetMaxSize()) {
        DrainQueueUntilSize(ctx.write_set_, 1);
        // there are spaces to insert
        leaf_page->ShiftKeyAndValueRight(insertPos);
        leaf_page->SetSize(leaf_page->GetSize() + 1);
        // insert
        leaf_page->SetKeyAt(insertPos, key);
        leaf_page->SetValueAt(insertPos, value);
        return true;
    }

    // no space left, have to merge or split
    // TODO
    return true;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::FindInsertPosition(LeafPage* page, const KeyType& key) const -> size_t {
    size_t left = 0, right = page->GetSize();
    while (left < right) {
        auto mid = left + (right - left) / 2;
        if (comparator_(page->KeyAt(mid), key) < 0) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }

    return left;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::IsKeyInTombstones(LeafPage* page, const KeyType& key) const -> bool {
    for (const auto& k: page->GetTombstones()) {
        if (comparator_(k, key) == 0){
            return true;
        }
    }
    return false;
}

/*****************************************************************************
 * REMOVE
 *****************************************************************************/
/**
 * @brief Delete key & value pair associated with input key
 * If current tree is empty, return immediately.
 * If not, User needs to first find the right leaf page as deletion target, then
 * delete entry from leaf page. Remember to deal with redistribute or merge if
 * necessary.
 *
 * @param key input key
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Remove(const KeyType &key) {
  // Declaration of context instance.
  Context ctx;
  UNIMPLEMENTED("TODO(P2): Add implementation.");
}

/*****************************************************************************
 * INDEX ITERATOR
 *****************************************************************************/
/**
 * @brief Input parameter is void, find the leftmost leaf page first, then construct
 * index iterator
 *
 * You may want to implement this while implementing Task #3.
 *
 * @return : index iterator
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin() -> INDEXITERATOR_TYPE { UNIMPLEMENTED("TODO(P2): Add implementation."); }

/**
 * @brief Input parameter is low key, find the leaf page that contains the input key
 * first, then construct index iterator
 * @return : index iterator
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin(const KeyType &key) -> INDEXITERATOR_TYPE { UNIMPLEMENTED("TODO(P2): Add implementation."); }

/**
 * @brief Input parameter is void, construct an index iterator representing the end
 * of the key/value pair in the leaf node
 * @return : index iterator
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::End() -> INDEXITERATOR_TYPE { UNIMPLEMENTED("TODO(P2): Add implementation."); }

/**
 * @return Page id of the root of this tree
 *
 * You may want to implement this while implementing Task #3.
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetRootPageId() -> page_id_t { UNIMPLEMENTED("TODO(P2): Add implementation."); }

template class BPlusTree<GenericKey<4>, RID, GenericComparator<4>>;

template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>>;
template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>, 3>;
template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>, 2>;
template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>, 1>;
template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>, -1>;

template class BPlusTree<GenericKey<16>, RID, GenericComparator<16>>;

template class BPlusTree<GenericKey<32>, RID, GenericComparator<32>>;

template class BPlusTree<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub
