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
  // Declaration of context instance. Using the Context is not necessary but advised.
  Context ctx;

  // root header page
  WritePageGuard guard = bpm_->WritePage(header_page_id_);
  auto header_page = guard.AsMut<BPlusTreeHeaderPage>();
  ctx.header_page_ = std::move(guard);
  ctx.root_page_id_ = header_page->root_page_id_;

  if (header_page->root_page_id_ == INVALID_PAGE_ID) {
    // tree is empty
    auto new_page_id = bpm_->NewPage();
    header_page->root_page_id_ = new_page_id;

    WritePageGuard leaf_guard = bpm_->WritePage(new_page_id);
    auto leaf_page = leaf_guard.AsMut<LeafPage>();
    leaf_page->Init();

    ctx.write_set_.emplace_back(std::move(leaf_guard));
  } else {
    WritePageGuard root_guard = bpm_->WritePage(header_page->root_page_id_);
    ctx.write_set_.emplace_back(std::move(root_guard));
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
  }

  auto leaf_guard = std::move(ctx.write_set_.back());
  ctx.write_set_.pop_back();

  auto leaf_page = leaf_guard.AsMut<LeafPage>();
  if (leaf_page->GetSize() == leaf_page->GetMaxSize()) {
    leaf_page->CompactTombstones();
  }
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

  auto old_page_size = old_page->GetSize();
  int mid = std::ceil(static_cast<double>(old_page_size) / 2);
  auto pushed_up_key = old_page->KeyAt(mid);

  for (auto i = mid; i < old_page_size; i += 1) {
    new_leaf_page->SetKeyAt(i - mid, old_page->KeyAt(i));
    new_leaf_page->SetValueAt(i - mid, old_page->ValueAt(i));
  }

  new_leaf_page->SetSize(old_page_size - mid);
  old_page->SetSize(mid);

  new_leaf_page->SetNextPageId(old_page->GetNextPageId());
  old_page->SetNextPageId(new_leaf_page_id);
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
