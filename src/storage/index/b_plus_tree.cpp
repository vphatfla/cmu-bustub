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
#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>
#include "buffer/traced_buffer_pool_manager.h"
#include "common/config.h"
#include "common/macros.h"
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

  // Fetch header page and check if tree is empty
  ReadPageGuard header_guard = bpm_->ReadPage(header_page_id_);
  auto header_page = header_guard.As<BPlusTreeHeaderPage>();
  if (header_page->root_page_id_ == INVALID_PAGE_ID) {
    return false;
  }

  // Fetch root and traverse to leaf, releasing parent guards along the way
  ctx.read_set_.emplace_back(bpm_->ReadPage(header_page->root_page_id_));
  TraverseNodesToLeaf(ctx.read_set_, key, true);

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

      result->emplace_back(leaf_page->ValueAt(mid));
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
  Context ctx;

  // root header page
  WritePageGuard guard = bpm_->WritePage(header_page_id_);
  auto header_page = guard.AsMut<BPlusTreeHeaderPage>();
  ctx.header_page_ = std::move(guard);
  ctx.root_page_id_ = header_page->root_page_id_;

  if (header_page->root_page_id_ == INVALID_PAGE_ID) {
    // tree is empty, create new leaf as root
    auto new_page_id = bpm_->NewPage();
    header_page->root_page_id_ = new_page_id;
    ctx.root_page_id_ = new_page_id;

    WritePageGuard leaf_guard = bpm_->WritePage(new_page_id);
    auto leaf_page = leaf_guard.AsMut<LeafPage>();
    leaf_page->Init();

    ctx.write_set_.emplace_back(std::move(leaf_guard));
  } else {
    // Fetch root and traverse to leaf, keeping all guards for potential splits
    ctx.write_set_.emplace_back(bpm_->WritePage(header_page->root_page_id_));
    TraverseNodesToLeaf(ctx.write_set_, key, false);
  }

  auto leaf_guard = std::move(ctx.write_set_.back());
  ctx.write_set_.pop_back();

  auto leaf_page = leaf_guard.AsMut<LeafPage>();
  if (leaf_page->GetSize() < leaf_page->GetMaxSize()) {
    return InsertKVToLeafPage(leaf_page, key, value);
  }
  // split
  auto [pushed_up_key, new_leaf_guard, new_leaf_id] = SplitLeafPage(leaf_page);

  auto new_leaf_page = new_leaf_guard.template AsMut<LeafPage>();
  if (comparator_(new_leaf_page->KeyAt(0), key) > 0) {
    auto rc = InsertKVToLeafPage(leaf_page, key, value);
    BUSTUB_ENSURE(rc, "Insert must usccess");
  } else {
    auto rc = InsertKVToLeafPage(new_leaf_page, key, value);
    BUSTUB_ENSURE(rc, "Insert must usccess");
  }

  InsertToParent(pushed_up_key, new_leaf_id, ctx);

  // drop all guard
  ctx.header_page_ = std::nullopt;
  DrainQueueUntilSize(ctx.write_set_, 0);
  return true;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::FindInsertPositionInLeafPage(LeafPage *page, const KeyType &key) const -> size_t {
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
auto BPLUSTREE_TYPE::FindInsertPositionInInternalPage(InternalPage *page, const KeyType &key) const -> size_t {
  size_t left = 1, right = page->GetSize();
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
void BPLUSTREE_TYPE::InsertToParent(const KeyType &key, const page_id_t page_id, Context &ctx) {
  if (ctx.write_set_.empty()) {
    // already splite the root, now create new root and update
    auto old_root_id = ctx.root_page_id_;
    auto [new_root_guard, new_root_id] = CreateNewRootAndUpdateHeader(ctx);
    auto new_root_page = new_root_guard.template AsMut<InternalPage>();
    new_root_page->SetSize(2);
    new_root_page->SetKeyAt(1, key);
    new_root_page->SetValueAt(0, old_root_id);
    new_root_page->SetValueAt(1, page_id);
    return;
  }

  WritePageGuard parent_guard = std::move(ctx.write_set_.back());
  ctx.write_set_.pop_back();

  auto parent_page = parent_guard.AsMut<InternalPage>();
  if (parent_page->GetSize() < parent_page->GetMaxSize()) {
    // insert to the parent
    InsertKVToInternalPage(parent_page, key, page_id);
    return;
  }
  // split this internal guard
  auto [pushed_up_key, new_page_guard, new_page_id] = SplitInternalPage(parent_page);

  if (comparator_(pushed_up_key, key) > 0) {
    InsertKVToInternalPage(parent_page, key, page_id);
  } else {
    auto new_page = new_page_guard.template AsMut<InternalPage>();
    InsertKVToInternalPage(new_page, key, page_id);
  }
  InsertToParent(pushed_up_key, new_page_id, ctx);
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::InsertKVToLeafPage(LeafPage *page, const KeyType &key, const ValueType &value) -> bool {
  BUSTUB_ENSURE(page->GetSize() < page->GetMaxSize(), "Does not have enough space to insert to leaf page");
  auto index_pos = FindInsertPositionInLeafPage(page, key);
  if (static_cast<int>(index_pos) < page->GetSize() && comparator_(page->KeyAt(index_pos), key) == 0) {
    // key exists
    // check if it's in tombstones
    if (page->IsIndexInTombstones(index_pos)) {
      page->SetKeyAt(index_pos, key);
      page->SetValueAt(index_pos, value);
      page->RemoveIndexFromTombstones(index_pos);
      return true;
    }
    return false;  // duplicated key, not allowed
  }

  page->ShiftKeyAndValueRight(index_pos);
  page->SetKeyAt(index_pos, key);
  page->SetValueAt(index_pos, value);
  page->SetSize(page->GetSize() + 1);
  return true;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::InsertKVToInternalPage(InternalPage *page, const KeyType &key, const page_id_t page_id) -> bool {
  BUSTUB_ENSURE(page->GetSize() < page->GetMaxSize(), "Internal page does not have space to insert new KV");
  auto index_pos = FindInsertPositionInInternalPage(page, key);

  page->ShiftKeyAndValueRight(index_pos);
  page->SetKeyAt(index_pos, key);
  page->SetValueAt(index_pos, page_id);
  page->SetSize(page->GetSize() + 1);
  return true;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::SplitLeafPage(LeafPage *old_page) -> std::tuple<KeyType, WritePageGuard, page_id_t> {
  auto new_leaf_page_id = bpm_->NewPage();
  auto new_leaf_guard = bpm_->WritePage(new_leaf_page_id);
  auto new_leaf_page = new_leaf_guard.AsMut<LeafPage>();
  new_leaf_page->Init(leaf_max_size_);

  size_t old_page_size = old_page->GetSize();
  size_t mid = std::ceil(static_cast<double>(old_page_size) / 2);
  auto pushed_up_key = old_page->KeyAt(mid);

  for (auto i = mid; i < old_page_size; i += 1) {
    new_leaf_page->SetKeyAt(i - mid, old_page->KeyAt(i));
    new_leaf_page->SetValueAt(i - mid, old_page->ValueAt(i));
  }

  new_leaf_page->SetSize(old_page_size - mid);
  old_page->SetSize(mid);

  new_leaf_page->SetNextPageId(old_page->GetNextPageId());
  old_page->SetNextPageId(new_leaf_page_id);

  auto old_tombstones_indexes = old_page->GetIndexesInTombstones();
  old_page->ClearTombstones();
  for (const auto &i : old_tombstones_indexes) {
    if (i < mid) {
      old_page->AddIndexToTombstones(i);
    } else {
      new_leaf_page->AddIndexToTombstones(i - mid);
    }
  }

  return {pushed_up_key, std::move(new_leaf_guard), new_leaf_page_id};
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::SplitInternalPage(InternalPage *old_page) -> std::tuple<KeyType, WritePageGuard, page_id_t> {
  auto new_internal_page_id = bpm_->NewPage();
  auto new_internal_guard = bpm_->WritePage(new_internal_page_id);
  auto new_internal_page = new_internal_guard.AsMut<InternalPage>();
  new_internal_page->Init(internal_max_size_);

  auto old_page_size = old_page->GetSize();
  auto mid = std::ceil(static_cast<double>(old_page_size) / 2);
  auto push_up_key = old_page->KeyAt(mid);

  auto new_page_index = 0;
  new_internal_page->SetValueAt(new_page_index, old_page->ValueAt(mid));

  for (auto i = mid + 1; i < old_page_size; i += 1) {
    new_internal_page->SetKeyAt(new_page_index + 1, old_page->KeyAt(i));
    new_internal_page->SetValueAt(new_page_index + 1, old_page->ValueAt(i));
    new_page_index += 1;
  }

  new_internal_page->SetSize(old_page_size - mid);
  old_page->SetSize(mid);

  return {push_up_key, std::move(new_internal_guard), new_internal_page_id};
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::CreateNewRootAndUpdateHeader(Context &ctx) -> std::pair<WritePageGuard, page_id_t> {
  BUSTUB_ENSURE(ctx.header_page_.has_value(), "Header page write guard must not be null");
  auto header_guard = std::move(ctx.header_page_.value());
  auto header_page = header_guard.AsMut<BPlusTreeHeaderPage>();

  auto new_root_page_id = bpm_->NewPage();
  WritePageGuard new_root_page_guard = bpm_->WritePage(new_root_page_id);
  auto root_page = new_root_page_guard.AsMut<InternalPage>();
  root_page->Init(internal_max_size_);

  header_page->root_page_id_ = new_root_page_id;
  ctx.root_page_id_ = new_root_page_id;
  ctx.header_page_ = std::move(header_guard);

  return {std::move(new_root_page_guard), new_root_page_id};
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

  // root header page
  WritePageGuard guard = bpm_->WritePage(header_page_id_);
  auto header_page = guard.AsMut<BPlusTreeHeaderPage>();
  ctx.header_page_ = std::move(guard);
  ctx.root_page_id_ = header_page->root_page_id_;

  if (header_page->root_page_id_ == INVALID_PAGE_ID) {
    // current page is empty
    return;
  } else {
    ctx.write_set_.emplace_back(bpm_->WritePage(header_page->root_page_id_));
    TraverseNodesToLeaf(ctx.write_set_, key, false);
  }

  auto leaf_guard = std::move(ctx.write_set_.back());
  ctx.write_set_.pop_back();
  auto leaf_page = leaf_guard.AsMut<LeafPage>();
  auto pos = FindIndexOfKeyInLeafPage(leaf_page, key);

  if (!pos.has_value()) {
    // key does not exist in leaf page
    return;
  }

  if (leaf_page->IsIndexInTombstones(pos.value())) {
    // key already in tombstones, return
    return;
  }

  auto new_logical_size = leaf_page->GetSize() - leaf_page->GetTombstonesSize() - 1;
  auto min_required_size = leaf_page->GetMinSize();
  if (new_logical_size >= min_required_size) {
    if (!leaf_page->IsTombstonesFull()) {
      // still have space, add the k-v to tombstone
      leaf_page->AddIndexToTombstones(pos.value());
      return;
    }
    leaf_page->DeleteOldestKeyInTombstones();
    auto new_pos = FindIndexOfKeyInLeafPage(leaf_page, key);
    leaf_page->AddIndexToTombstones(new_pos.value());
    return;
  }

  // Adding the key to tombstone before redistribute/merge
  if (!leaf_page->IsTombstonesFull()) {
    leaf_page->AddIndexToTombstones(pos.value());
  } else {
    leaf_page->DeleteOldestKeyInTombstones();
    auto new_pos = FindIndexOfKeyInLeafPage(leaf_page, key);
    leaf_page->AddIndexToTombstones(new_pos.value());
  }

  if (ctx.write_set_.empty()) {
    // This is root page - can be underfilled
    // Check if root is now empty (all entries are tombstones)
    if (leaf_page->GetSize() == static_cast<int>(leaf_page->GetTombstonesSize())) {
      // Root leaf is empty, set tree to empty
      auto header_guard = std::move(ctx.header_page_.value());
      auto header_page = header_guard.AsMut<BPlusTreeHeaderPage>();
      header_page->root_page_id_ = INVALID_PAGE_ID;
    }
    return;
  }

  auto parent_guard = std::move(ctx.write_set_.back());
  ctx.write_set_.pop_back();
  auto parent_page = parent_guard.AsMut<InternalPage>();

  auto child_index = parent_page->ValueIndex(leaf_guard.GetPageId());

  // Try redistribute from left sibling
  auto left_guard_opt = GetLeftSiblingPage(parent_page, child_index);
  if (left_guard_opt.has_value()) {
    auto left_sibling_page = left_guard_opt.value().template AsMut<LeafPage>();
    if (RedistributeLeafPageLeftSibling(leaf_page, left_sibling_page)) {
      // Update parent separator key: parent key at child_index = first key of curr_page
      parent_page->SetKeyAt(child_index, leaf_page->KeyAt(0));
      return;
    }
  }

  // Try redistribute from right sibling
  auto right_guard_opt = GetRightSiblingPage(parent_page, child_index);
  if (right_guard_opt.has_value()) {
    auto right_sibling_page = right_guard_opt.value().template AsMut<LeafPage>();
    if (RedistributeLeafPageRightSibling(leaf_page, right_sibling_page)) {
      // Update parent separator key: parent key at child_index+1 = first key of right sibling
      parent_page->SetKeyAt(child_index + 1, right_sibling_page->KeyAt(0));
      return;
    }
  }

  // Neither sibling can spare - must merge
  // Merge with left sibling if exists, otherwise merge with right
  if (left_guard_opt.has_value()) {
    auto left_sibling_page = left_guard_opt.value().template AsMut<LeafPage>();
    MergeTwoLeafPages(left_sibling_page, leaf_page);
    // TODO: Remove key at child_index from parent, handle cascading
  } else if (right_guard_opt.has_value()) {
    auto right_sibling_page = right_guard_opt.value().template AsMut<LeafPage>();
    MergeTwoLeafPages(leaf_page, right_sibling_page);
    // TODO: Remove key at child_index+1 from parent, handle cascading
  }
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::FindIndexOfKeyInLeafPage(LeafPage *page, const KeyType &key) const -> std::optional<size_t> {
  auto left = 0, right = page->GetSize() - 1;
  while (left <= right) {
    auto mid = left + (right - left) / 2;
    auto cpm = comparator_(page->KeyAt(mid), key);
    if (cpm == 0) {
      return mid;
    } else if (cpm < 0) {
      left = mid + 1;
    } else {
      right = mid - 1;
    }
  }
  return std::nullopt;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetLeftSiblingPage(InternalPage *parent_page, int child_index) -> std::optional<WritePageGuard> {
  if (child_index > 0) {
    auto left_sibling_id = parent_page->ValueAt(child_index - 1);
    WritePageGuard sibling_guard = bpm_->WritePage(left_sibling_id);
    return std::move(sibling_guard);
  }
  return std::nullopt;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetRightSiblingPage(InternalPage *parent_page, int child_index) -> std::optional<WritePageGuard> {
  if (child_index < parent_page->GetSize() - 1) {
    auto right_sibling_id = parent_page->ValueAt(child_index + 1);
    WritePageGuard sibling_guard = bpm_->WritePage(right_sibling_id);
    return std::move(sibling_guard);
  }
  return std::nullopt;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::RedistributeLeafPageLeftSibling(LeafPage *curr_page, LeafPage *sibling_page) -> bool {
  auto sibling_logical_size = sibling_page->GetSize() - sibling_page->GetTombstonesSize();
  auto sibling_min_required_size = sibling_page->GetMinSize();
  if (sibling_logical_size <= sibling_min_required_size) {
    return false;  // can't spare any k-v
  }

  auto curr_logical_size = curr_page->GetSize() - curr_page->GetTombstonesSize();
  auto curr_min_required_size = curr_page->GetMinSize();
  auto sibling_index = sibling_page->GetSize() - 1;
  while (curr_logical_size < curr_min_required_size &&
         (sibling_page->GetSize() - sibling_page->GetTombstonesSize()) > sibling_min_required_size) {
    auto key = sibling_page->KeyAt(sibling_index);
    auto value = sibling_page->ValueAt(sibling_index);
    curr_page->ShiftKeyAndValueRight(0);
    curr_page->SetKeyAt(0, key);
    curr_page->SetValueAt(0, value);
    curr_page->IncrementAllTombstonesIndexes();

    curr_page->SetSize(curr_page->GetSize() + 1);
    sibling_page->SetSize(sibling_page->GetSize() - 1);
    if (sibling_page->IsIndexInTombstones(sibling_index)) {
      if (curr_page->IsTombstonesFull()) {
        curr_page->DeleteOldestKeyInTombstones();
      }
      curr_page->AddIndexToTombstones(0);  // getting key from left sibling
      sibling_page->RemoveIndexFromTombstones(sibling_index);
    }

    curr_logical_size = curr_page->GetSize() - curr_page->GetTombstonesSize();
    sibling_index -= 1;
  }
  // Return true only if curr_page reached min_size
  return curr_logical_size >= curr_min_required_size;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::RedistributeLeafPageRightSibling(LeafPage *curr_page, LeafPage *sibling_page) -> bool {
  auto sibling_logical_size = sibling_page->GetSize() - sibling_page->GetTombstonesSize();
  auto sibling_min_required_size = sibling_page->GetMinSize();
  if (sibling_logical_size <= sibling_min_required_size) {
    return false;  // can't spare any k-v
  }

  auto curr_logical_size = curr_page->GetSize() - curr_page->GetTombstonesSize();
  auto curr_min_required_size = curr_page->GetMinSize();
  auto curr_index = curr_page->GetSize();
  while (curr_logical_size < curr_min_required_size &&
         (sibling_page->GetSize() - sibling_page->GetTombstonesSize()) > sibling_min_required_size) {
    auto key = sibling_page->KeyAt(0);
    auto value = sibling_page->ValueAt(0);
    curr_page->SetKeyAt(curr_index, key);
    curr_page->SetValueAt(curr_index, value);

    sibling_page->ShiftKeyAndValueLeft(0);
    if (sibling_page->IsIndexInTombstones(0)) {
      if (curr_page->IsTombstonesFull()) {
        curr_page->DeleteOldestKeyInTombstones();
      }
      curr_page->AddIndexToTombstones(curr_index);  // getting key from right sibling
      sibling_page->RemoveIndexFromTombstones(0);
    }

    sibling_page->DecreaseAllTombstonesIndexes();
    curr_page->SetSize(curr_page->GetSize() + 1);
    sibling_page->SetSize(sibling_page->GetSize() - 1);
    curr_logical_size = curr_page->GetSize() - curr_page->GetTombstonesSize();
    curr_index += 1;
  }
  // Return true only if curr_page reached min_size
  return curr_logical_size >= curr_min_required_size;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::MergeTwoLeafPages(LeafPage *dest_page, LeafPage *src_page) {
  // Merge src_page INTO dest_page (src_page will be deleted by caller)
  // Collect tombstoned KEYS from both pages (in FIFO order: dest oldest first)
  auto dest_tomb_keys = dest_page->GetTombstones();
  auto src_tomb_keys = src_page->GetTombstones();

  // Clear dest's tombstones for fresh rebuild
  dest_page->ClearTombstones();

  auto dest_size = dest_page->GetSize();
  auto src_size = src_page->GetSize();

  // Copy all entries from src_page to dest_page
  for (int i = 0; i < src_size; i++) {
    dest_page->SetKeyAt(dest_size + i, src_page->KeyAt(i));
    dest_page->SetValueAt(dest_size + i, src_page->ValueAt(i));
  }
  dest_page->SetSize(dest_size + src_size);

  // Rebuild tombstones: add dest's first (older), then src's (newer)
  // When evicting, DeleteOldestKeyInTombstones adjusts remaining indices automatically
  for (const auto &key : dest_tomb_keys) {
    if (dest_page->IsTombstonesFull()) {
      dest_page->DeleteOldestKeyInTombstones();
    }
    auto idx = FindIndexOfKeyInLeafPage(dest_page, key);
    if (idx.has_value()) {
      dest_page->AddIndexToTombstones(idx.value());
    }
  }

  for (const auto &key : src_tomb_keys) {
    if (dest_page->IsTombstonesFull()) {
      dest_page->DeleteOldestKeyInTombstones();
    }
    auto idx = FindIndexOfKeyInLeafPage(dest_page, key);
    if (idx.has_value()) {
      dest_page->AddIndexToTombstones(idx.value());
    }
  }

  // Update sibling pointer: dest now points to src's next
  dest_page->SetNextPageId(src_page->GetNextPageId());
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::RemoveKeyValueInInternalPage(Context &ctx, WritePageGuard guard, size_t child_index) {
  auto page = guard.AsMut<InternalPage>();
  page->ShiftKeyAndValueRight(child_index + 1);
  page->SetSize(page->GetSize() - 1);

  if (page->GetSize() >= page->GetMinSize()) {
    return;
  }

  if (ctx.IsRootPage(guard.GetPageId())) {
    return;
  }

  // get the parent of this page
  WritePageGuard parent_guard = std::move(ctx.write_set_.back());
  ctx.write_set_.pop_back();
  auto parent_page = parent_guard.AsMut<InternalPage>();
  auto curr_child_index = parent_page->ValueIndex(guard.GetPageId());
  // current page is underfilled, need to fetch the left and right sibling for redistribute or merging
  // try left
  auto left_sibling_guard = GetLeftSiblingPage(parent_page, curr_child_index);
  if (left_sibling_guard.has_value()) {
    // todo: redistribute
  }
  // try right
  auto right_sibling_guard = GetRightSiblingPage(parent_page, curr_child_index);
  if (right_sibling_guard.has_value()) {
    // TODO
  }

  // TODO
  // try merging
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::RedistributeInternalPageLeftSibling(InternalPage *curr_page, InternalPage *sibling_page,
                                                         InternalPage *parent_page, const int curr_child_index)
    -> bool {
  if (sibling_page->GetSize() <= sibling_page->GetMinSize()) {
    return false;
  }

  auto n = sibling_page->GetSize();
  // shift right in the current page
  curr_page->ShiftKeyAndValueRight(1);
  // pull the key from parent page
  curr_page->SetKeyAt(1, parent_page->KeyAt(curr_child_index));
  // borrow the pointer from sibiling
  curr_page->SetValueAt(0, sibling_page->ValueAt(n - 1));
  // push the key up from sibling page
  parent_page->SetKeyAt(curr_child_index, sibling_page->KeyAt(n - 1));

  // decrease k-v count from sibling page, shoudn't have to physicially delete
  sibling_page->SetSize(n - 1);
  // increase curr page size
  curr_page->SetSize(curr_page->GetSize() + 1);
  return true;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::RedistributeInternalPageRightSibling(InternalPage *curr_page, InternalPage *sibling_page,
                                                          InternalPage *parent_page, const int curr_child_index)
    -> bool {
  if (sibling_page->GetSize() <= sibling_page->GetMinSize()) {
    return false;
  }

  auto n = curr_page->GetSize();
  // pull down the key from parent to curr_page
  curr_page->SetKeyAt(n, parent_page->KeyAt(curr_child_index + 1));
  // push up the left most key from the sibiling
  parent_page->SetKeyAt(curr_child_index + 1, sibling_page->KeyAt(1));
  // curr_page to borrow the left-most pointer
  curr_page->SetValueAt(n, sibling_page->ValueAt(0));

  // shift key and value in sibiling page after lending the left most key
  sibling_page->SetValueAt(0, sibling_page->ValueAt(1));
  sibling_page->ShiftKeyAndValueLeft(1);

  // adjust size
  sibling_page->SetSize(sibling_page->GetSize() - 1);
  curr_page->SetSize(n + 1);
  return true;
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
auto BPLUSTREE_TYPE::GetRootPageId() -> page_id_t {
  ReadPageGuard guard = bpm_->ReadPage(header_page_id_);
  auto header_page = guard.As<BPlusTreeHeaderPage>();
  return header_page->root_page_id_;
}

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
