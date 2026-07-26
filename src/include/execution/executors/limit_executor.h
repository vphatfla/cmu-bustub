//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// limit_executor.h
//
// Identification: src/include/execution/executors/limit_executor.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

#include "common/rid.h"
#include "execution/executors/abstract_executor.h"
#include "execution/plans/limit_plan.h"
#include "storage/table/tuple.h"

namespace bustub {

/**
 * LimitExecutor limits the number of output tuples produced by a child operator.
 */
class LimitExecutor : public AbstractExecutor {
 public:
  LimitExecutor(ExecutorContext *exec_ctx, const LimitPlanNode *plan,
                std::unique_ptr<AbstractExecutor> &&child_executor);

  void Init() override;

  auto Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch, size_t batch_size)
      -> bool override;

  /** @return The output schema for the limit */
  auto GetOutputSchema() const -> const Schema & override { return plan_->OutputSchema(); };

 private:
  /** The limit plan node to be executed */
  const LimitPlanNode *plan_;

  /** The child executor from which tuples are obtained */
  std::unique_ptr<AbstractExecutor> child_executor_;

  // @brief buffer cache for the tuples streaming from child_executor_
  std::vector<Tuple> child_tuples_;
  // @brief buffer cache for the rids streaming from child_executor_
  std::vector<RID> child_rids_;
  // @brief index tracker for the in mem cache tuples from the child executor
  size_t child_tuple_index_{0};

  // @brief mutable tracker for limit tuples
  size_t limit_{0};
};
}  // namespace bustub
