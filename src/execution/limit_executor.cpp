//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// limit_executor.cpp
//
// Identification: src/execution/limit_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/limit_executor.h"
#include <utility>
#include "common/config.h"
#include "common/macros.h"

namespace bustub {

/**
 * Construct a new LimitExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The limit plan to be executed
 * @param child_executor The child executor from which limited tuples are pulled
 */
LimitExecutor::LimitExecutor(ExecutorContext *exec_ctx, const LimitPlanNode *plan,
                             std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

/** Initialize the limit */
void LimitExecutor::Init() {
  child_executor_->Init();

  child_tuples_.clear();
  child_rids_.clear();
  child_tuple_index_ = 0;

  limit_ = plan_->GetLimit();
}

/**
 * Yield the next tuple batch from the limit.
 * @param[out] tuple_batch The next tuple batch produced by the limit
 * @param[out] rid_batch The next tuple RID batch produced by the limit
 * @param batch_size The number of tuples to be included in the batch (default: BUSTUB_BATCH_SIZE)
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 */
auto LimitExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                         size_t batch_size) -> bool {
  tuple_batch->clear();
  rid_batch->clear();
  tuple_batch->reserve(batch_size);
  rid_batch->reserve(batch_size);

  while (batch_size > 0 && limit_ > 0) {
    if (child_tuple_index_ >= child_tuples_.size()) {
      // need to reload the child tuples
      if (!child_executor_->Next(&child_tuples_, &child_rids_, BUSTUB_BATCH_SIZE)) {
        break;
      }
      child_tuple_index_ = 0;
    }
    while (batch_size > 0 && limit_ > 0 && child_tuple_index_ < child_tuples_.size()) {
      tuple_batch->emplace_back(child_tuples_[child_tuple_index_]);
      rid_batch->emplace_back(child_rids_[child_tuple_index_]);

      child_tuple_index_ += 1;
      batch_size -= 1;
      limit_ -= 1;
    }
  }

  return !tuple_batch->empty();
}

}  // namespace bustub
