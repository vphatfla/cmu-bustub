//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// seq_scan_executor.cpp
//
// Identification: src/execution/seq_scan_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/seq_scan_executor.h"
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>
#include "common/macros.h"

namespace bustub {

/**
 * Construct a new SeqScanExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The sequential scan plan to be executed
 */
SeqScanExecutor::SeqScanExecutor(ExecutorContext *exec_ctx, const SeqScanPlanNode *plan)
    : AbstractExecutor(exec_ctx), plan_(plan) {}

/** Initialize the sequential scan */
void SeqScanExecutor::Init() {
  table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->table_name_);
  table_iterator_.emplace(table_info_->table_->MakeIterator());
}

/**
 * Yield the next tuple batch from the seq scan.
 * @param[out] tuple_batch The next tuple batch produced by the scan
 * @param[out] rid_batch The next tuple RID batch produced by the scan
 * @param batch_size The number of tuples to be included in the batch (default: BUSTUB_BATCH_SIZE)
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 */
auto SeqScanExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                           size_t batch_size) -> bool {
  tuple_batch->clear();
  rid_batch->clear();

  tuple_batch->reserve(batch_size);
  rid_batch->reserve(batch_size);

  while (!table_iterator_->IsEnd() && batch_size > 0) {
    auto [tuplemeta, tuple] = table_iterator_->GetTuple();
    if (tuplemeta.is_deleted_) {
      ++table_iterator_.value();
      continue;
    }
    if (plan_->filter_predicate_ != nullptr) {
      auto result = plan_->filter_predicate_->Evaluate(&tuple, table_info_->schema_);
      if (result.IsNull() || !result.GetAs<bool>()) {
        ++table_iterator_.value();
        continue;
      }
    }

    rid_batch->emplace_back(tuple.GetRid());
    tuple_batch->emplace_back(std::move(tuple));
    batch_size--;
    ++table_iterator_.value();
  }

  return !tuple_batch->empty();
}

}  // namespace bustub
