//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// index_scan_executor.cpp
//
// Identification: src/execution/index_scan_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/index_scan_executor.h"
#include <optional>
#include <vector>
#include "common/macros.h"
#include "storage/index/b_plus_tree_index.h"
#include "storage/table/tuple.h"
#include "type/value.h"

namespace bustub {

/**
 * Creates a new index scan executor.
 * @param exec_ctx the executor context
 * @param plan the index scan plan to be executed
 */
IndexScanExecutor::IndexScanExecutor(ExecutorContext *exec_ctx, const IndexScanPlanNode *plan)
    : AbstractExecutor(exec_ctx), plan_(plan) {}

void IndexScanExecutor::Init() {
  table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->table_oid_);
  table_iterator_.emplace(table_info_->table_->MakeIterator());

  index_info_ = exec_ctx_->GetCatalog()->GetIndex(plan_->index_oid_);
  tree_ = dynamic_cast<BPlusTreeIndexForTwoIntegerColumn *>(index_info_->index_.get());
  tree_iterator_.emplace(tree_->GetBeginIterator());
}

auto IndexScanExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                             size_t batch_size) -> bool {
  tuple_batch->clear();
  rid_batch->clear();
  tuple_batch->reserve(batch_size);
  rid_batch->reserve(batch_size);

  if (!plan_->pred_keys_.empty()) {
    // use constant point look up index
    auto key_values = std::vector<Value>{};
    for (const auto &pred_key : plan_->pred_keys_) {
      // get the constant value in the pred
      key_values.emplace_back(pred_key->Evaluate(nullptr, GetOutputSchema()));
    }

    auto index_key_tuple = Tuple{key_values, &index_info_->key_schema_};
    // todo scan key here
  }
}

}  // namespace bustub
