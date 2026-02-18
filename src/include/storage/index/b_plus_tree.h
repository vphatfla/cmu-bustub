//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree.h
//
// Identification: src/include/storage/index/b_plus_tree.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

/**
 * b_plus_tree.h
 *
 * Implementation of simple b+ tree data structure where internal pages direct
 * the search and leaf pages contain actual data.
 * (1) We only support unique key
 * (2) support insert & remove
 * (3) The structure should shrink and grow dynamically
 * (4) Implement index iterator for range scan
 */
#pragma once

#include <algorithm>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <queue>
#include <shared_mutex>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "common/config.h"
#include "common/macros.h"
#include "storage/index/index_iterator.h"
#include "storage/page/b_plus_tree_header_page.h"
#include "storage/page/b_plus_tree_internal_page.h"
#include "storage/page/b_plus_tree_leaf_page.h"
#include "storage/page/b_plus_tree_page.h"
#include "storage/page/page_guard.h"

namespace bustub {

struct PrintableBPlusTree;

/**
 * @brief Definition of the Context class.
 *
 * Hint: This class is designed to help you keep track of the pages
 * that you're modifying or accessing.
 */
class Context {
 public:
  // When you insert into / remove from the B+ tree, store the write guard of header page here.
  // Remember to drop the header page guard and set it to nullopt when you want to unlock all.
  std::optional<WritePageGuard> header_page_{std::nullopt};

  // Save the root page id here so that it's easier to know if the current page is the root page.
  page_id_t root_page_id_{INVALID_PAGE_ID};

  // Store the write guards of the pages that you're modifying here.
  std::deque<WritePageGuard> write_set_;

  // You may want to use this when getting value, but not necessary.
  std::deque<ReadPageGuard> read_set_;

  auto IsRootPage(page_id_t page_id) -> bool { return page_id == root_page_id_; }
};

#define BPLUSTREE_TYPE BPlusTree<KeyType, ValueType, KeyComparator, NumTombs>

// Main class providing the API for the Interactive B+ Tree.
FULL_INDEX_TEMPLATE_ARGUMENTS_DEFN
class BPlusTree {
  using InternalPage = BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator>;
  using LeafPage = BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>;

 public:
  explicit BPlusTree(std::string name, page_id_t header_page_id, BufferPoolManager *buffer_pool_manager,
                     const KeyComparator &comparator, int leaf_max_size = LEAF_PAGE_SLOT_CNT,
                     int internal_max_size = INTERNAL_PAGE_SLOT_CNT);

  // Returns true if this B+ tree has no keys and values.
  auto IsEmpty() const -> bool;

  // Insert a key-value pair into this B+ tree.
  auto Insert(const KeyType &key, const ValueType &value) -> bool;

  // Remove a key and its value from this B+ tree.
  void Remove(const KeyType &key);

  // Return the value associated with a given key
  auto GetValue(const KeyType &key, std::vector<ValueType> *result) -> bool;

  // Return the page id of the root node
  auto GetRootPageId() -> page_id_t;

  // Index iterator
  auto Begin() -> INDEXITERATOR_TYPE;

  auto End() -> INDEXITERATOR_TYPE;

  auto Begin(const KeyType &key) -> INDEXITERATOR_TYPE;

  void Print(BufferPoolManager *bpm);

  void Draw(BufferPoolManager *bpm, const std::filesystem::path &outf);

  auto DrawBPlusTree() -> std::string;

  // read data from file and insert one by one
  void InsertFromFile(const std::filesystem::path &file_name);

  // read data from file and remove one by one
  void RemoveFromFile(const std::filesystem::path &file_name);

  void BatchOpsFromFile(const std::filesystem::path &file_name);

  // Do not change this type to a BufferPoolManager!
  std::shared_ptr<TracedBufferPoolManager> bpm_;

 private:
  void ToGraph(page_id_t page_id, const BPlusTreePage *page, std::ofstream &out);

  void PrintTree(page_id_t page_id, const BPlusTreePage *page);

  auto ToPrintableBPlusTree(page_id_t root_id) -> PrintableBPlusTree;

  // member variable
  std::string index_name_;
  KeyComparator comparator_;
  std::vector<std::string> log;  // NOLINT
  int leaf_max_size_;
  int internal_max_size_;
  page_id_t header_page_id_;

  [[nodiscard]] auto FindIndexOfKeyInLeafPage(LeafPage *page, const KeyType &key) const -> std::optional<size_t>;
  [[nodiscard]] auto FindInsertPositionInLeafPage(LeafPage *page, const KeyType &key) const -> size_t;
  [[nodiscard]] auto FindInsertPositionInInternalPage(InternalPage *page, const KeyType &key) const -> size_t;

  /// @brief Push up the key and page id after split on INSERT
  /// This can be called after splitting of either LEAF or INTERNAL notes
  void InsertToParent(const KeyType &key, const page_id_t page_id, Context &ctx);

  /// @brief Insert the key/value to the leaf node, increase the node size
  /// Caller must ensure that there is space for this insertion
  auto InsertKVToLeafPage(LeafPage *page, const KeyType &key, const ValueType &value) -> bool;

  /// @brief Insert the key/value to the internal page, increase the node size
  /// Caller must ensure that there is space for this insertion
  auto InsertKVToInternalPage(InternalPage *page, const KeyType &key, const page_id_t page_id) -> bool;

  /// @brief Split the Leaf Page and set size for both old and new page, caller must ensure the condition to split is
  /// correct
  /// @return a unique ptr of the new leaf page WritePageGuard, caller must flush the new page
  [[nodiscard]] auto SplitLeafPage(LeafPage *old_page) -> std::tuple<KeyType, WritePageGuard, page_id_t>;

  /// @brief Split the Internal Page and set size for both old and new page, caller must ensure condition to split is
  /// correct
  /// @return tuple [pushed up key, guard of new internal page, page id of the new page]
  [[nodiscard]] auto SplitInternalPage(InternalPage *old_page) -> std::tuple<KeyType, WritePageGuard, page_id_t>;

  /// @brief Create new root page, and update the ctx
  /// @return the pair <WritePageGuard, page_id> of the new root page
  [[nodiscard]] auto CreateNewRootAndUpdateHeader(Context &ctx) -> std::pair<WritePageGuard, page_id_t>;

  /// @brief Get the sibling write guard of the given child page (works for both leaf and internal)
  auto GetLeftSiblingPage(InternalPage *parent_page, int child_index) -> std::optional<WritePageGuard>;
  auto GetRightSiblingPage(InternalPage *parent_page, int child_index) -> std::optional<WritePageGuard>;

  /// @brief Redistributed the sibling with the current leaf page
  /// @return return false if failed to redistribute, else true
  auto RedistributeLeafPageLeftSibling(LeafPage *curr_page, LeafPage *sibling_page) -> bool;

  /// @brief Redistributed the sibling with the current leaf page
  /// @return return false if failed to redistribute, else true
  auto RedistributeLeafPageRightSibling(LeafPage *curr_page, LeafPage *sibling_page) -> bool;

  /// @brief Redistribute the sibiling the the current internal page
  /// @param curr_page: page that is borrowing key and pointer
  /// @param sibling_page: page that is lending out key and pointer
  /// @param parent_page: parent of both curr_page and sibling_page
  /// @param curr_child_index: is the index of curr_page in key_array of parent_page
  auto RedistributeInternalPageLeftSibling(InternalPage *curr_page, InternalPage *sibling_page,
                                           InternalPage *parent_page, const int curr_child_index) -> bool;

  /// @brief Redistribute the sibiling the the current internal page
  /// @param curr_page: page that is borrowing key and pointer
  /// @param sibling_page: page that is lending out key and pointer
  /// @param parent_page: parent of both curr_page and sibling_page
  /// @param curr_child_index: is the index of curr_page in key_array of parent_page
  auto RedistributeInternalPageRightSibling(InternalPage *curr_page, InternalPage *sibling_page,
                                            InternalPage *parent_page, const int curr_child_index) -> bool;

  /// @brief Merge src_page INTO dest_page (src_page entries are copied to dest_page)
  void MergeTwoLeafPages(LeafPage *dest_page, LeafPage *src_page);
  /// @brief Given a key, traverse all the way to the leaf node that contains the key
  /// @note Caller must push root guard onto guard_set before calling. For Insert, caller handles header page
  /// separately.
  template <typename GuardType>
  void TraverseNodesToLeaf(std::deque<GuardType> &guard_set, const KeyType &key, const bool release_parent) {
    static_assert(std::is_same_v<GuardType, WritePageGuard> || std::is_same_v<GuardType, ReadPageGuard>,
                  "GuardType must be either WritePageGuard or ReadPageGuard");

    while (true) {
      // Use As<> for read-only access during traversal (works for both guard types)
      auto curr_page = guard_set.back().template As<BPlusTreePage>();
      if (curr_page->IsLeafPage()) {
        return;
      }

      auto curr_internal_page = guard_set.back().template As<InternalPage>();
      auto left = 1, right = curr_internal_page->GetSize() - 1;
      while (left <= right) {
        auto mid = left + (right - left) / 2;
        if (comparator_(key, curr_internal_page->KeyAt(mid)) >= 0) {
          left = mid + 1;
        } else {
          right = mid - 1;
        }
      }

      auto next_page_id = curr_internal_page->ValueAt(right);

      if constexpr (std::is_same_v<GuardType, WritePageGuard>) {
        guard_set.emplace_back(bpm_->WritePage(next_page_id));
      } else {
        guard_set.emplace_back(bpm_->ReadPage(next_page_id));
      }

      if (release_parent) {
        guard_set.pop_front();
      }
    }
  }

  /// @brief Handle remove the Key and Value pair in InternalPage
  /// This is used during the merging of sibling children
  /// @param page: parent InternalPage pointer
  /// @param child_index: the index of the left sibling that we're preserve
  void RemoveKeyValueInInternalPage(Context &ctx, WritePageGuard guard, size_t child_index);
};

/**
 * @brief for test only. PrintableBPlusTree is a printable B+ tree.
 * We first convert B+ tree into a printable B+ tree and the print it.
 */
struct PrintableBPlusTree {
  int size_;
  std::string keys_;
  std::vector<PrintableBPlusTree> children_;

  /**
   * @brief BFS traverse a printable B+ tree and print it into
   * into out_buf
   *
   * @param out_buf
   */
  void Print(std::ostream &out_buf) {
    std::vector<PrintableBPlusTree *> que = {this};
    while (!que.empty()) {
      std::vector<PrintableBPlusTree *> new_que;

      for (auto &t : que) {
        int padding = (t->size_ - t->keys_.size()) / 2;
        out_buf << std::string(padding, ' ');
        out_buf << t->keys_;
        out_buf << std::string(padding, ' ');

        for (auto &c : t->children_) {
          new_que.push_back(&c);
        }
      }
      out_buf << "\n";
      que = new_que;
    }
  }
};

// utility func to drain the queue

template <typename T>
inline void DrainQueueUntilSize(std::deque<T> &queue, const size_t size) {
  static_assert(std::is_same_v<T, ReadPageGuard> || std::is_same_v<T, WritePageGuard>,
                "Type must be read page guard or write page guard");
  while (queue.size() > size) {
    queue.pop_front();  // pop the guard top down
  }
}

}  // namespace bustub
