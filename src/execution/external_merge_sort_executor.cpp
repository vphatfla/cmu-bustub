//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// external_merge_sort_executor.cpp
//
// Identification: src/execution/external_merge_sort_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/external_merge_sort_executor.h"
#include <algorithm>
#include <cstddef>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>
#include "common/config.h"
#include "common/macros.h"
#include "common/rid.h"
#include "execution/execution_common.h"
#include "execution/plans/sort_plan.h"
#include "storage/page/intermediate_result_page.h"
#include "storage/table/tuple.h"

namespace bustub {

template <size_t K>
ExternalMergeSortExecutor<K>::ExternalMergeSortExecutor(ExecutorContext *exec_ctx, const SortPlanNode *plan,
                                                        std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), cmp_(plan->GetOrderBy()), child_executor_(std::move(child_executor)) {}

/** Initialize the external merge sort */
template <size_t K>
void ExternalMergeSortExecutor<K>::Init() {
  child_executor_->Init();
  merge_sort_runs_.clear();

  auto child_tuples = std::vector<Tuple>{};
  auto child_rids = std::vector<RID>{};

  // Size each P0 run off a memory budget proportional to the ACTUAL buffer pool (not a fixed
  // literal), mirroring the classic external-sort design of using (B - reserve) buffer pages for
  // the initial sorted run. Reserve a couple of pages for the concurrent input/output pages
  // MergeTwoRuns touches during later merge passes. A tiny fixed chunk (e.g. BUSTUB_BATCH_SIZE)
  // produces far too many runs/merge rounds for large tables, blowing past disk I/O budgets under
  // a constrained buffer pool; tying the budget to the pool size keeps it correct across pool sizes.
  constexpr size_t merge_overhead_reserve_pages = 2;
  const size_t bpm_size = exec_ctx_->GetBufferPoolManager()->Size();
  const size_t run_page_budget = bpm_size > merge_overhead_reserve_pages ? bpm_size - merge_overhead_reserve_pages : 1;
  const size_t run_byte_budget = run_page_budget * BUSTUB_PAGE_SIZE;

  auto chunk_tuples = std::vector<Tuple>{};
  size_t chunk_bytes = 0;

  while (true) {
    bool child_has_more = child_executor_->Next(&child_tuples, &child_rids, BUSTUB_BATCH_SIZE);
    for (const auto &tuple : child_tuples) {
      chunk_bytes += tuple.GetLength();
      chunk_tuples.emplace_back(tuple);
    }

    // Flush the accumulated chunk into its own run once it reaches the byte budget, or once the
    // child is exhausted (whatever remains becomes the final, possibly smaller, run). A run must
    // never span more than one flush, otherwise it wouldn't be internally sorted end-to-end.
    bool should_flush = !chunk_tuples.empty() && (chunk_bytes >= run_byte_budget || !child_has_more);
    if (should_flush) {
      auto sort_entries = std::vector<SortEntry>{};
      sort_entries.reserve(chunk_tuples.size());
      for (const auto &tuple : chunk_tuples) {
        sort_entries.emplace_back(GenerateSortKey(tuple, plan_->GetOrderBy(), child_executor_->GetOutputSchema()),
                                  tuple);
      }
      std::sort(sort_entries.begin(), sort_entries.end(), cmp_);

      // this chunk becomes its own new run, spanning as many pages as it needs
      merge_sort_runs_.emplace_back(MergeSortRun{std::vector<page_id_t>{}, exec_ctx_->GetBufferPoolManager()});
      for (const auto &e : sort_entries) {
        merge_sort_runs_.back().InsertTuple(e.second);
      }

      chunk_tuples.clear();
      chunk_bytes = 0;
    }

    if (!child_has_more) {
      break;
    }
  }

  RecursiveMerge();
  BUSTUB_ASSERT(merge_sort_runs_.size() <= 1, "Merge sort runs must have at MOST ONE run at final phase");

  if (!merge_sort_runs_.empty()) {
    it_ = merge_sort_runs_[0].Begin();
  }
}

/**
 * Yield the next tuple batch from the external merge sort.
 * @param[out] tuple_batch The next tuple batch produced by the external merge sort.
 * @param[out] rid_batch The next tuple RID batch produced by the external merge sort.
 * @param batch_size The number of tuples to be included in the batch (default: BUSTUB_BATCH_SIZE)
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 */
template <size_t K>
auto ExternalMergeSortExecutor<K>::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                                        size_t batch_size) -> bool {
  tuple_batch->clear();
  rid_batch->clear();

  tuple_batch->reserve(batch_size);
  rid_batch->reserve(batch_size);

  BUSTUB_ASSERT(merge_sort_runs_.size() <= 1, "Merge sort runs must have at MOST ONE run at final phase");

  if (merge_sort_runs_.empty()) {
    return false;
  }

  while (batch_size > 0 && it_ != merge_sort_runs_[0].End()) {
    tuple_batch->emplace_back(*it_);
    rid_batch->emplace_back(RID{});
    ++it_;

    batch_size -= 1;
  }

  return !tuple_batch->empty();
}

template <size_t K>
void ExternalMergeSortExecutor<K>::RecursiveMerge() {
  auto new_level_merge_sort_runs = std::vector<MergeSortRun>{};

  if (merge_sort_runs_.size() <= 1) {
    return;
  }

  for (size_t i = 0; i < merge_sort_runs_.size(); i += 2) {
    if (i + 1 < merge_sort_runs_.size()) {
      auto r1 = std::move(merge_sort_runs_[i]);
      auto r2 = std::move(merge_sort_runs_[i + 1]);
      new_level_merge_sort_runs.emplace_back(MergeTwoRuns(r1, r2));
      r1.DeleteAllPages();
      r2.DeleteAllPages();
    } else {
      new_level_merge_sort_runs.emplace_back(std::move(merge_sort_runs_[i]));
    }
  }

  merge_sort_runs_.clear();
  merge_sort_runs_ = std::move(new_level_merge_sort_runs);

  RecursiveMerge();
}

template <size_t K>
auto ExternalMergeSortExecutor<K>::MergeTwoRuns(MergeSortRun &r1, MergeSortRun &r2) -> MergeSortRun {
  auto msr_result = MergeSortRun{std::vector<page_id_t>{}, exec_ctx_->GetBufferPoolManager()};

  auto iterator_1 = r1.Begin();
  auto iterator_2 = r2.Begin();

  while (iterator_1 != r1.End() || iterator_2 != r2.End()) {
    if (iterator_1 == r1.End()) {
      // flush the rest of r2
      msr_result.InsertTuple(*iterator_2);
      ++iterator_2;
      continue;
    }
    if (iterator_2 == r2.End()) {
      // flush the rest of r1
      msr_result.InsertTuple(*iterator_1);
      ++iterator_1;
      continue;
    }

    auto tuple_1 = *iterator_1;
    auto tuple_2 = *iterator_2;

    auto sort_entry_1 =
        SortEntry{GenerateSortKey(tuple_1, plan_->GetOrderBy(), child_executor_->GetOutputSchema()), tuple_1};
    auto sort_entry_2 =
        SortEntry{GenerateSortKey(tuple_2, plan_->GetOrderBy(), child_executor_->GetOutputSchema()), tuple_2};

    if (cmp_(sort_entry_1, sort_entry_2)) {
      msr_result.InsertTuple(*iterator_1);
      ++iterator_1;
    } else {
      msr_result.InsertTuple(*iterator_2);
      ++iterator_2;
    }
  }

  return msr_result;
}

template class ExternalMergeSortExecutor<2>;

}  // namespace bustub
