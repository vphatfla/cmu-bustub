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

  index_info_ = exec_ctx_->GetCatalog()->GetIndex(plan_->index_oid_);
  tree_ = dynamic_cast<BPlusTreeIndexForTwoIntegerColumn *>(index_info_->index_.get());

  BUSTUB_ASSERT(tree_ != nullptr, "tree can not be null");
  // executor to use contant point look up via plan.pred_keys
  if (!plan_->pred_keys_.empty()) {
    rid_.clear();
    rid_pos_ = 0;
    // use constant point look up index
    for (const auto &pred_key : plan_->pred_keys_) {
      auto key_value = pred_key->Evaluate(nullptr, GetOutputSchema());
      auto index_key_tuple = Tuple{std::vector<Value>{key_value}, &index_info_->key_schema_};
      tree_->ScanKey(index_key_tuple, &rid_, exec_ctx_->GetTransaction());
    }
    return;
  }

  // executor to be used for ordered scann - reset tree iterator
  tree_iterator_.emplace(tree_->GetBeginIterator());
}

auto IndexScanExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                             size_t batch_size) -> bool {
  tuple_batch->clear();
  rid_batch->clear();
  tuple_batch->reserve(batch_size);
  rid_batch->reserve(batch_size);

  if (isPointLookup()) {
    if (rid_pos_ >= rid_.size()) {
      return false;
    }
    while (batch_size > 0 && rid_pos_ < rid_.size()) {
      auto rid = rid_[rid_pos_];

      auto [meta, tuple] = table_info_->table_->GetTuple(rid);
      if (!meta.is_deleted_) {
        rid_batch->emplace_back(rid);
        tuple_batch->emplace_back(tuple);
        batch_size -= 1;
      }
      rid_pos_ += 1;
    }
  } else {
    // ordered scan
    if (tree_iterator_->IsEnd()) {
      return false;
    }
    while (batch_size > 0 && !tree_iterator_->IsEnd()) {
      auto [k, rid] = *tree_iterator_.value();

      auto [meta, tuple] = table_info_->table_->GetTuple(rid);
      if (!meta.is_deleted_) {
        rid_batch->emplace_back(rid);
        tuple_batch->emplace_back(tuple);
        batch_size -= 1;
      }
      ++tree_iterator_.value();
    }
  }
  return !tuple_batch->empty();
}

auto IndexScanExecutor::isPointLookup() -> bool { return !plan_->pred_keys_.empty(); }

}  // namespace bustub
