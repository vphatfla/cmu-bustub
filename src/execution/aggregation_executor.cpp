//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// aggregation_executor.cpp
//
// Identification: src/execution/aggregation_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <memory>
#include <tuple>
#include <utility>
#include <vector>
#include "common/config.h"
#include "common/macros.h"
#include "common/rid.h"
#include "execution/plans/abstract_plan.h"
#include "storage/table/tuple.h"
#include "type/value.h"

#include "execution/executors/aggregation_executor.h"

namespace bustub {

/**
 * Construct a new AggregationExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The insert plan to be executed
 * @param child_executor The child executor from which inserted tuples are pulled (may be `nullptr`)
 */
AggregationExecutor::AggregationExecutor(ExecutorContext *exec_ctx, const AggregationPlanNode *plan,
                                         std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      child_executor_(std::move(child_executor)),
      aht_(plan_->GetAggregates(), plan_->GetAggregateTypes()),
      aht_iterator_(aht_.Begin()) {}

/** Initialize the aggregation */
void AggregationExecutor::Init() {
  aht_.Clear();
  child_executor_->Init();

  auto c_tuple_batch = std::vector<Tuple>{};
  auto c_rid_batch = std::vector<RID>{};
  c_tuple_batch.reserve(BUSTUB_BATCH_SIZE);
  c_rid_batch.reserve(BUSTUB_BATCH_SIZE);

  while (child_executor_->Next(&c_tuple_batch, &c_rid_batch, BUSTUB_BATCH_SIZE)) {
    for (const auto &t : c_tuple_batch) {
      aht_.InsertCombine(MakeAggregateKey(&t), MakeAggregateValue(&t));
    }
    c_tuple_batch.clear();
    c_rid_batch.clear();
    c_tuple_batch.reserve(BUSTUB_BATCH_SIZE);
    c_rid_batch.reserve(BUSTUB_BATCH_SIZE);
  }

  // if table is empty and has no group by, then retured of Next() should be one row with 0 for CountStar and null for
  // the others
  if (plan_->group_bys_.empty() && aht_.Begin() == aht_.End()) {
    aht_.InsertInitial(AggregateKey{});  // empty key
  }

  aht_iterator_ = aht_.Begin();
}

/**
 * Yield the next tuple batch from the aggregation.
 * @param[out] tuple_batch The next batch of tuples produced by the aggregation
 * @param[out] rid_batch The next batch of tuple RIDs produced by the aggregation
 * @param batch_size The number of tuples to be included in the batch (default: BUSTUB_BATCH_SIZE)
 * @return `true` if any tuples were produced, `false` if there are no more tuples
 */

auto AggregationExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                               size_t batch_size) -> bool {
  tuple_batch->clear();
  rid_batch->clear();
  tuple_batch->reserve(batch_size);
  rid_batch->reserve(batch_size);

  while (batch_size > 0 && aht_iterator_ != aht_.End()) {
    auto values = std::vector<Value>{};
    values.reserve(GetOutputSchema().GetColumnCount());

    for (const auto &gb : aht_iterator_.Key().group_bys_) {
      values.emplace_back(gb);
    }

    for (const auto &ag : aht_iterator_.Val().aggregates_) {
      values.emplace_back(ag);
    }

    tuple_batch->emplace_back(Tuple{values, &GetOutputSchema()});
    rid_batch->emplace_back();

    ++aht_iterator_;
    batch_size -= 1;
  }

  return !tuple_batch->empty();
}

/** Do not use or remove this function; otherwise, you will get zero points. */
auto AggregationExecutor::GetChildExecutor() const -> const AbstractExecutor * { return child_executor_.get(); }

}  // namespace bustub
