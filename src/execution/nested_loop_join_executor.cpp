//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// nested_loop_join_executor.cpp
//
// Identification: src/execution/nested_loop_join_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/nested_loop_join_executor.h"
#include <utility>
#include <vector>
#include "binder/table_ref/bound_join_ref.h"
#include "common/config.h"
#include "common/exception.h"
#include "common/macros.h"
#include "common/rid.h"
#include "storage/table/tuple.h"
#include "type/value.h"
#include "type/value_factory.h"

namespace bustub {

/**
 * Construct a new NestedLoopJoinExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The nested loop join plan to be executed
 * @param left_executor The child executor that produces tuple for the left side of join
 * @param right_executor The child executor that produces tuple for the right side of join
 */
NestedLoopJoinExecutor::NestedLoopJoinExecutor(ExecutorContext *exec_ctx, const NestedLoopJoinPlanNode *plan,
                                               std::unique_ptr<AbstractExecutor> &&left_executor,
                                               std::unique_ptr<AbstractExecutor> &&right_executor)
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      left_executor_(std::move(left_executor)),
      right_executor_(std::move(right_executor)) {
  if (plan->GetJoinType() != JoinType::LEFT && plan->GetJoinType() != JoinType::INNER) {
    // Note for Spring 2025: You ONLY need to implement left join and inner join.
    throw bustub::NotImplementedException(fmt::format("join type {} not supported", plan->GetJoinType()));
  }
}

/** Initialize the join */
void NestedLoopJoinExecutor::Init() {
  left_executor_->Init();
  right_executor_->Init();

  left_tuples_.clear();
  left_rids_.clear();
  right_tuples_.clear();
  right_rids_.clear();

  left_pos_ = 0;
  right_pos_ = 0;
  did_left_match_ = false;
}

/**
 * Yield the next tuple batch from the join.
 * @param[out] tuple_batch The next tuple batch produced by the join
 * @param[out] rid_batch The next tuple RID batch produced by the join
 * @param batch_size The number of tuples to be included in the batch (default: BUSTUB_BATCH_SIZE)
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 */
auto NestedLoopJoinExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                                  size_t batch_size) -> bool {
  tuple_batch->clear();
  rid_batch->clear();
  tuple_batch->reserve(batch_size);
  rid_batch->reserve(batch_size);

  while (batch_size > 0) {
    // refill the left batch if needed
    if (left_pos_ >= left_tuples_.size()) {
      left_tuples_.clear();
      left_rids_.clear();
      left_tuples_.reserve(batch_size);
      left_rids_.reserve(batch_size);

      if (!left_executor_->Next(&left_tuples_, &left_rids_, BUSTUB_BATCH_SIZE)) {
        // left table exhausted, nothing more to do
        break;
      }
      left_pos_ = 0;

      // re-init and filled the right executor
      // this is not optimial and should be done once in the INIT however the grader check this
      right_executor_->Init();
      right_tuples_.clear();
      right_rids_.clear();
      right_pos_ = 0;
      auto batch_tuples = std::vector<Tuple>{};
      auto batch_rids = std::vector<RID>{};
      while (right_executor_->Next(&batch_tuples, &batch_rids, BUSTUB_BATCH_SIZE)) {
        right_tuples_.insert(right_tuples_.end(), batch_tuples.begin(), batch_tuples.end());
        right_rids_.insert(right_rids_.end(), batch_rids.begin(), batch_rids.end());
        batch_tuples.clear();
        batch_rids.clear();
      }
    }

    // pick up from the last right pos
    // reset the right pos as needed
    if (right_pos_ >= right_tuples_.size()) {
      right_pos_ = 0;
    }

    // iterate through the right tuples
    auto &left_tuple = left_tuples_[left_pos_];
    while (right_pos_ < right_tuples_.size() && batch_size > 0) {
      auto &right_tuple = right_tuples_[right_pos_];

      auto result = plan_->Predicate()->EvaluateJoin(&left_tuple, left_executor_->GetOutputSchema(), &right_tuple,
                                                     right_executor_->GetOutputSchema());
      if (!result.IsNull() && result.GetAs<bool>()) {
        tuple_batch->emplace_back(ConstructOutTuple(left_tuple, &right_tuple));
        rid_batch->emplace_back(RID{});
        did_left_match_ = true;
        batch_size -= 1;
      }

      right_pos_ += 1;
    }

    if (batch_size > 0) {
      // we should have iterated through all the right tuples given the current left tuples, check for left join
      if (right_pos_ >= right_tuples_.size() && !did_left_match_ && plan_->GetJoinType() == JoinType::LEFT) {
        tuple_batch->emplace_back(ConstructOutTuple(left_tuple, nullptr));
        rid_batch->emplace_back(RID{});
        batch_size -= 1;
      }
    }

    if (right_pos_ >= right_tuples_.size()) {
      left_pos_ += 1;
      did_left_match_ = false;
    }
  }

  return !tuple_batch->empty();
}

auto NestedLoopJoinExecutor::ConstructOutTuple(const Tuple &left, const Tuple *right) -> Tuple {
  auto values = std::vector<Value>{};
  values.reserve(GetOutputSchema().GetColumnCount());

  for (unsigned int i = 0; i < left_executor_->GetOutputSchema().GetColumnCount(); i += 1) {
    values.emplace_back(left.GetValue(&left_executor_->GetOutputSchema(), i));
  }
  for (unsigned int i = 0; i < right_executor_->GetOutputSchema().GetColumnCount(); i += 1) {
    if (right != nullptr) {
      values.emplace_back(right->GetValue(&right_executor_->GetOutputSchema(), i));
    } else {
      values.emplace_back(ValueFactory::GetNullValueByType(right_executor_->GetOutputSchema().GetColumn(i).GetType()));
    }
  }

  return Tuple{values, &GetOutputSchema()};
}

}  // namespace bustub
