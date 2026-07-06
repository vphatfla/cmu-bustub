//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// nested_index_join_executor.h
//
// Identification: src/include/execution/executors/nested_index_join_executor.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "catalog/catalog.h"
#include "common/rid.h"
#include "execution/executor_context.h"
#include "execution/executors/abstract_executor.h"
#include "execution/plans/nested_index_join_plan.h"
#include "storage/table/tuple.h"

namespace bustub {

/**
 * NestedIndexJoinExecutor executes index join operations.
 */
class NestedIndexJoinExecutor : public AbstractExecutor {
 public:
  NestedIndexJoinExecutor(ExecutorContext *exec_ctx, const NestedIndexJoinPlanNode *plan,
                          std::unique_ptr<AbstractExecutor> &&child_executor);

  /** @return The output schema for the nested index join */
  auto GetOutputSchema() const -> const Schema & override { return plan_->OutputSchema(); }

  void Init() override;

  auto Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch, size_t batch_size)
      -> bool override;

 private:
  /** The nested index join plan node. */
  const NestedIndexJoinPlanNode *plan_;

  /** child executor, by convention this should be the outter table */
  std::unique_ptr<AbstractExecutor> child_executor_;

  /** outter table vector buffer tuples */
  std::vector<Tuple> outter_tuples_{};

  /** outter table vector buffer rids */
  std::vector<RID> outter_rids_{};

  /** outter tuples tracker */
  size_t outter_tuple_pos_{0};

  /** inner table info */
  std::shared_ptr<TableInfo> inner_table_info_;

  /** inner index info */
  std::shared_ptr<IndexInfo> inner_index_info_;

  /** vector buffer for rids given the index query using the current outter tuple */
  std::vector<RID> inner_rids_{};

  /** pos tracker for the buffered rids inner_rids_ */
  size_t inner_rid_pos_{0};

  /** bool tracker for the inner rids if any matched with the outter tuple, used for LEFT JOIN logic */
  bool did_outter_tuple_match_{false};

  /** bool tracker to indicate should we fetch the rids for the outter tuples or use the buffered rids vector */
  bool should_fetch_inner_rids_{true};

  /** @brief Private helper to construct the output tuple for this executor */
  auto ConstructOutputTuple(const Tuple &left, const Tuple *right) -> Tuple;
};
}  // namespace bustub
