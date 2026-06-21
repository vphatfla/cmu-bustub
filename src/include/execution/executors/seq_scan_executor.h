//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// seq_scan_executor.h
//
// Identification: src/include/execution/executors/seq_scan_executor.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "catalog/catalog.h"
#include "execution/executor_context.h"
#include "execution/executors/abstract_executor.h"
#include "execution/plans/seq_scan_plan.h"
#include "storage/table/table_iterator.h"
#include "storage/table/tuple.h"

namespace bustub {

/**
 * The SeqScanExecutor executor executes a sequential table scan.
 */
class SeqScanExecutor : public AbstractExecutor {
 public:
  SeqScanExecutor(ExecutorContext *exec_ctx, const SeqScanPlanNode *plan);

  void Init() override;

  auto Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch, size_t batch_size)
      -> bool override;

  /** @return The output schema for the sequential scan */
  auto GetOutputSchema() const -> const Schema & override { return plan_->OutputSchema(); }

 private:
  /** The sequential scan plan node to be executed */
  const SeqScanPlanNode *plan_;
  /** Table Info, hold table heap and table schema */
  std::shared_ptr<TableInfo> table_info_{};
  /** Table Iterator for table table in table_info_ */
  std::optional<TableIterator> table_iterator_{};
};
}  // namespace bustub
