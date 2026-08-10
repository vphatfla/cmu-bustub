//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// external_merge_sort_executor.h
//
// Identification: src/include/execution/executors/external_merge_sort_executor.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>
#include "common/config.h"
#include "common/macros.h"
#include "execution/execution_common.h"
#include "execution/executors/abstract_executor.h"
#include "execution/plans/sort_plan.h"
#include "storage/page/intermediate_result_page.h"
#include "storage/table/tuple.h"

namespace bustub {

/**
 * A data structure that holds the sorted tuples as a run during external merge sort.
 * Tuples might be stored in multiple pages, and tuples are ordered both within one page
 * and across pages.
 */
class MergeSortRun {
 public:
  MergeSortRun() = default;
  MergeSortRun(std::vector<page_id_t> pages, BufferPoolManager *bpm) : pages_(std::move(pages)), bpm_(bpm) {}

  auto GetPageCount() -> size_t { return pages_.size(); }

  // MOVED
  MergeSortRun(MergeSortRun &&other) noexcept = default;
  auto operator=(MergeSortRun &&other) noexcept -> MergeSortRun & = default;
  /** Iterator for iterating on the sorted tuples in one run. */
  class Iterator {
    friend class MergeSortRun;

   public:
    Iterator() = default;

    /**
     * Advance the iterator to the next tuple. If the current sort page is exhausted, move to the
     * next sort page.
     */
    auto operator++() -> Iterator & {
      Iterator *it = this;
      it->tuple_idx_ += 1;

      // check if we should move up page index
      auto read_guard = run_->bpm_->ReadPage(run_->pages_[page_idx_]);
      const auto *page = read_guard.As<IntermediateResultPage>();

      if (it->tuple_idx_ >= page->GetNumTuples()) {
        if (it->page_idx_ < run_->pages_.size()) {
          it->page_idx_ += 1;
        }
        it->tuple_idx_ = 0;
      }
      return *it;
    }

    /**
     * Dereference the iterator to get the current tuple in the sorted run that the iterator is
     * pointing to.
     */
    auto operator*() -> Tuple {
      auto read_guard = run_->bpm_->ReadPage(run_->pages_[page_idx_]);
      const auto *page = read_guard.As<IntermediateResultPage>();

      return page->GetTupleAtIndex(tuple_idx_);
    }

    /**
     * Checks whether two iterators are pointing to the same tuple in the same sorted run.
     */
    auto operator==(const Iterator &other) const -> bool {
      return run_ == other.run_ && page_idx_ == other.page_idx_ && tuple_idx_ == other.tuple_idx_;
    }

    /**
     * Checks whether two iterators are pointing to different tuples in a sorted run or iterating
     * on different sorted runs.
     */
    auto operator!=(const Iterator &other) const -> bool {
      return run_ != other.run_ || page_idx_ != other.page_idx_ || tuple_idx_ != other.tuple_idx_;
    }

   private:
    explicit Iterator(const MergeSortRun *run) : run_(run) {}

    /** The sorted run that the iterator is iterating on. */
    [[maybe_unused]] const MergeSortRun *run_;

    // tracker index for the page and tuple within that page
    size_t page_idx_{0};
    // tracker index for the tuple within the current page that the iterator is pointing to
    size_t tuple_idx_{0};
  };

  /**
   * Get an iterator pointing to the beginning of the sorted run, i.e. the first tuple.
   */
  auto Begin() -> Iterator {
    auto it = Iterator{this};
    it.page_idx_ = 0;
    it.tuple_idx_ = 0;

    return it;
  }

  /**
   * Get an iterator pointing to the end of the sorted run, i.e. the position after the last tuple.
   */
  auto End() -> Iterator {
    auto it = Iterator{this};
    it.page_idx_ = pages_.size();
    it.tuple_idx_ = 0;

    return it;
  }

  // @brief abstracted insert tuple into this run
  // internally, this can span out a new page id
  void InsertTuple(const Tuple &tuple) {
    if (pages_.empty()) {
      pages_.emplace_back(bpm_->NewPage());

      auto write_guard = bpm_->WritePage(pages_[0]);
      write_guard.AsMut<IntermediateResultPage>()->Init();
    }

    auto write_guard = bpm_->WritePage(pages_[pages_.size() - 1]);

    if (!write_guard.AsMut<IntermediateResultPage>()->InsertTuple(tuple)) {
      // previous tuple full, need new one
      pages_.emplace_back(bpm_->NewPage());
      write_guard = bpm_->WritePage(pages_[pages_.size() - 1]);
      auto *page = write_guard.AsMut<IntermediateResultPage>();
      page->Init();

      BUSTUB_ASSERT(page->InsertTuple(tuple), "New page insert must success");
    }
  }

  // @brief delete all the page that this run holds
  // caller must make sure that all the page that this run holds will not be used
  void DeleteAllPages() {
    for (const auto &pid : pages_) {
      bpm_->DeletePage(pid);
    }
  }

 private:
  /** The page IDs of the sort pages that store the sorted tuples. */
  std::vector<page_id_t> pages_;
  /**
   * The buffer pool manager used to read sort pages. The buffer pool manager is responsible for
   * deleting the sort pages when they are no longer needed.
   */
  [[maybe_unused]] BufferPoolManager *bpm_;
};

/**
 * ExternalMergeSortExecutor executes an external merge sort.
 *
 * In Spring 2025, only 2-way external merge sort is required.
 */
template <size_t K>
class ExternalMergeSortExecutor : public AbstractExecutor {
 public:
  ExternalMergeSortExecutor(ExecutorContext *exec_ctx, const SortPlanNode *plan,
                            std::unique_ptr<AbstractExecutor> &&child_executor);

  void Init() override;

  auto Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch, size_t batch_size)
      -> bool override;

  /** @return The output schema for the external merge sort */
  auto GetOutputSchema() const -> const Schema & override { return plan_->OutputSchema(); }

 private:
  /** The sort plan node to be executed */
  const SortPlanNode *plan_;

  /** Compares tuples based on the order-bys */
  TupleComparator cmp_;

  std::unique_ptr<AbstractExecutor> child_executor_;

  // iterator tracker for this executor, it_ could contains a null ptr indicate if not yet Init() or there is zero
  // tuples available to emit in the executor
  MergeSortRun::Iterator it_;

  // store the current recursive pass of merge sort runs
  // sorting will stop once this vector hit size = 1
  // at each level, this vector must hold runs that have the same number of pages (1,2,...)
  std::vector<MergeSortRun> merge_sort_runs_;

  // @brief private helper to recursively merge the merge_sort_runs_
  // stop when merge_sort_runs_ has exactly 1 run
  void RecursiveMerge();

  // @brief merge K runs (2 in this case requirement)
  auto MergeTwoRuns(MergeSortRun &r1, MergeSortRun &r2) -> MergeSortRun;
};

}  // namespace bustub
