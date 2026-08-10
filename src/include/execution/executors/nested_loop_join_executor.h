//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// nested_loop_join_executor.h
//
// Identification: src/include/execution/executors/nested_loop_join_executor.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "common/rid.h"
#include "execution/executor_context.h"
#include "execution/executors/abstract_executor.h"
#include "execution/plans/nested_loop_join_plan.h"
#include "storage/table/tuple.h"

namespace bustub {

/**
 * NestedLoopJoinExecutor executes a nested-loop JOIN on two tables.
 */
class NestedLoopJoinExecutor : public AbstractExecutor {
 public:
  NestedLoopJoinExecutor(ExecutorContext *exec_ctx, const NestedLoopJoinPlanNode *plan,
                         std::unique_ptr<AbstractExecutor> &&left_executor,
                         std::unique_ptr<AbstractExecutor> &&right_executor);

  void Init() override;

  auto Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch, size_t batch_size)
      -> bool override;

  /** @return The output schema for the insert */
  auto GetOutputSchema() const -> const Schema & override { return plan_->OutputSchema(); };

 private:
  /** The NestedLoopJoin plan node to be executed. */
  const NestedLoopJoinPlanNode *plan_;

  /** Left child executor, by convention this is the smaller table */
  std::unique_ptr<AbstractExecutor> left_executor_;

  /** Right child executor */
  std::unique_ptr<AbstractExecutor> right_executor_;

  /** Tuples from the left table, max size = batch size, smaller table-> used for streaming*/
  std::vector<Tuple> left_tuples_{};

  /** Tuples from the left table, max size = batch size, smaller table -> used for streaming */
  std::vector<RID> left_rids_{};

  /** Index POS tracker for the tuple from left table in the left_tuples_batch */
  size_t left_pos_{0};

  /** All the tuple of the right table, materialized at Init() */
  std::vector<Tuple> right_tuples_{};

  /** All the rid of the right table, materialized at Init() */
  std::vector<RID> right_rids_{};

  /** Index POS tracker for the tuple from right table in the right_tuples */
  size_t right_pos_{0};

  /** bool track to check if the left tuple was matched with any right tuple, used for left join */
  bool did_left_match_{false};

  /** @brief private helper to construct the tuple given this executor output schema */
  auto ConstructOutTuple(const Tuple &left, const Tuple *right) -> Tuple;
};

}  // namespace bustub
