//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// window_function_executor.cpp
//
// Identification: src/execution/window_function_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/window_function_executor.h"
#include <algorithm>
#include <cstddef>
#include <numeric>
#include <unordered_map>
#include <utility>
#include <vector>
#include "binder/bound_order_by.h"
#include "common/config.h"
#include "common/macros.h"
#include "common/rid.h"
#include "execution/execution_common.h"
#include "execution/executors/aggregation_executor.h"
#include "execution/plans/aggregation_plan.h"
#include "execution/plans/window_plan.h"
#include "storage/table/tuple.h"
#include "type/type_id.h"
#include "type/value.h"
#include "type/value_factory.h"

namespace bustub {

/**
 * Construct a new WindowFunctionExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The window aggregation plan to be executed
 */
WindowFunctionExecutor::WindowFunctionExecutor(ExecutorContext *exec_ctx, const WindowFunctionPlanNode *plan,
                                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

/** Initialize the window aggregation */
void WindowFunctionExecutor::Init() {
  child_executor_->Init();
  child_tuples_.clear();
  postion_tracker_ = 0;
  window_func_results_.clear();
  presentation_index_order_.clear();

  // exhaust the child, assume that all tuples will fit in mem
  auto temp_tuples = std::vector<bustub::Tuple>{};
  auto temp_rids = std::vector<RID>{};
  while (child_executor_->Next(&temp_tuples, &temp_rids, BUSTUB_BATCH_SIZE)) {
    child_tuples_.insert(child_tuples_.end(), temp_tuples.begin(), temp_tuples.end());
  }

  const auto &child_tuple_schema = child_executor_->GetOutputSchema();
  for (const auto &[col_idx, window_func] : plan_->window_functions_) {
    // ----- for each window_func, generate the bucket of indicies based on the partition by key  -----
    // ----- example (PARTITION BY v1) --> v1 = {3.5}, then partitions = { 3->{i1, i5, ..}, 5->{i0, i3, ..} }
    auto partitions = std::unordered_map<AggregateKey, std::vector<size_t>>{};
    for (size_t i = 0; i < child_tuples_.size(); i += 1) {
      auto values = std::vector<Value>{};
      for (const auto &pb_expr : window_func.partition_by_) {
        values.emplace_back(pb_expr->Evaluate(&child_tuples_[i], child_tuple_schema));
      }
      auto key = AggregateKey{values};
      partitions[key].emplace_back(i);
    }

    // ---- init the results of this window_func ready ---
    window_func_results_[col_idx] = std::vector<Value>{};
    auto &window_func_result = window_func_results_[col_idx];
    window_func_result.resize(child_tuples_.size());

    // ---- decide ONCE per window_func (not per partition bucket) whether this function's own
    // ---- sorted traversal should become the presentation order for Next() -- a single function's
    // ---- buckets only form a complete ordering once ALL of its partitions have contributed
    const bool should_capture_presentation_order = presentation_index_order_.empty() && !window_func.order_by_.empty();

    // ---- for each window_func buckets, sort (if present), then compute the result ----
    for (auto &[key, indices] : partitions) {
      if (window_func.order_by_.empty()) {
        // no order by -> use all tuples for the window func call
        auto value_over_bucket = InitBucketValue(window_func.type_);
        for (const auto idx : indices) {
          CombineValueOverBucket(window_func.type_, &value_over_bucket,
                                 window_func.function_->Evaluate(&child_tuples_[idx], child_tuple_schema));
        }

        // for each tuple index within this bucket, populate the same value, since there is no order
        for (const auto idx : indices) {
          window_func_result[idx] = value_over_bucket;
        }

        continue;
      }

      // order_by present
      // ---- sort the buckets, then compute the result ----
      auto sorted_bucket = std::vector<std::pair<size_t, SortKey>>{};
      for (const auto idx : indices) {
        sorted_bucket.emplace_back(idx, GenerateSortKey(child_tuples_[idx], window_func.order_by_, child_tuple_schema));
      }
      auto tuple_cmp = TupleComparator{window_func.order_by_};
      std::sort(sorted_bucket.begin(), sorted_bucket.end(), [&](const auto &a, const auto &b) {
        return tuple_cmp(SortEntry{a.second, Tuple{}}, SortEntry{b.second, Tuple{}});
      });

      if (should_capture_presentation_order) {
        for (const auto &[idx, sorted_key] : sorted_bucket) {
          presentation_index_order_.emplace_back(idx);
        }
      }

      // ---- compute the result of the bucket based on the sorted indices ----
      if (window_func.type_ == WindowFunctionType::Rank) {
        // rank is 1-index based, if ties -> shared a rank
        int running_rank = 0;
        for (size_t i = 0; i < sorted_bucket.size(); i += 1) {
          const auto &[tuple_idx, sorted_key] = sorted_bucket[i];
          // tuple_cmp only support less than comparison
          const bool is_currently_tied =
              running_rank > 0 &&
              !tuple_cmp(SortEntry{sorted_key, Tuple{}}, SortEntry{sorted_bucket[i - 1].second, Tuple{}}) &&
              !tuple_cmp(SortEntry{sorted_bucket[i - 1].second, Tuple{}}, SortEntry{sorted_key, Tuple{}});

          if (!is_currently_tied) {
            running_rank = i + 1;
            // if nothing ties, then the rank is its position plus 1 (since 1-base rank)
          }

          window_func_result[tuple_idx] = ValueFactory::GetIntegerValue(running_rank);
        }
      } else {
        // other window function: sum, max, min ...
        auto running_value = InitBucketValue(window_func.type_);
        for (const auto &[idx, sorted_key] : sorted_bucket) {
          CombineValueOverBucket(window_func.type_, &running_value,
                                 window_func.function_->Evaluate(&child_tuples_[idx], child_tuple_schema));
          window_func_result[idx] = running_value;
        }
      }
    }
  }
}

/**
 * Yield the next tuple batch from the window aggregation.
 * @param[out] tuple_batch The next tuple batch produced by the window aggregation
 * @param[out] rid_batch The next tuple RID batch produced by the window aggregation
 * @param batch_size The number of tuples to be included in the batch (default: BUSTUB_BATCH_SIZE)
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 */
auto WindowFunctionExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                                  size_t batch_size) -> bool {
  tuple_batch->clear();
  rid_batch->clear();
  tuple_batch->reserve(batch_size);
  rid_batch->reserve(batch_size);

  const auto &child_tuple_schema = child_executor_->GetOutputSchema();
  while (batch_size > 0 && postion_tracker_ < child_tuples_.size()) {
    const auto tuple_index =
        presentation_index_order_.empty() ? postion_tracker_ : presentation_index_order_[postion_tracker_];
    // build the values for the output tuple
    auto values = std::vector<Value>{};
    for (size_t col_idx = 0; col_idx < plan_->columns_.size(); col_idx += 1) {
      if (auto it = window_func_results_.find(static_cast<uint32_t>(col_idx)); it != window_func_results_.end()) {
        values.emplace_back(it->second[tuple_index]);
      } else {
        values.emplace_back(plan_->columns_[col_idx]->Evaluate(&child_tuples_[tuple_index], child_tuple_schema));
      }
    }
    tuple_batch->emplace_back(Tuple{std::move(values), &plan_->OutputSchema()});
    rid_batch->emplace_back(RID{});
    batch_size -= 1;
    postion_tracker_ += 1;
  }

  return !tuple_batch->empty();
}

auto WindowFunctionExecutor::InitBucketValue(WindowFunctionType wft) const -> Value {
  if (wft == WindowFunctionType::CountStarAggregate) {
    return ValueFactory::GetIntegerValue(0);
  }
  return ValueFactory::GetNullValueByType(TypeId::INTEGER);
}

void WindowFunctionExecutor::CombineValueOverBucket(WindowFunctionType wft, Value *out_value,
                                                    const Value &in_value) const {
  switch (wft) {
    case bustub::WindowFunctionType::CountStarAggregate:
      *out_value = out_value->Add(ValueFactory::GetIntegerValue(1));
      return;
    case bustub::WindowFunctionType::CountAggregate:
      if (!in_value.IsNull()) {
        if (out_value->IsNull()) {
          *out_value = ValueFactory::GetIntegerValue(0);
        }
        *out_value = out_value->Add(ValueFactory::GetIntegerValue(1));
      }
      return;
    case bustub::WindowFunctionType::MaxAggregate:
      if (!in_value.IsNull()) {
        if (out_value->IsNull()) {
          *out_value = in_value;
        }
        *out_value = out_value->Max(in_value);
      }
      return;

    case bustub::WindowFunctionType::MinAggregate:
      if (!in_value.IsNull()) {
        if (out_value->IsNull()) {
          *out_value = in_value;
        }
        *out_value = out_value->Min(in_value);
      }
      return;

    case bustub::WindowFunctionType::SumAggregate:
      if (!in_value.IsNull()) {
        if (out_value->IsNull()) {
          *out_value = ValueFactory::GetIntegerValue(0);
        }
        *out_value = out_value->Add(in_value);
      }
      return;
    default:
      UNREACHABLE("Rank handled separately");
  }
}
}  // namespace bustub
