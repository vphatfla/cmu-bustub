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
#include "common/macros.h"

namespace bustub {

/**
 * Creates a new index scan executor.
 * @param exec_ctx the executor context
 * @param plan the index scan plan to be executed
 */
IndexScanExecutor::IndexScanExecutor(ExecutorContext *exec_ctx, const IndexScanPlanNode *plan)
    : AbstractExecutor(exec_ctx), plan_(plan) {
}

void IndexScanExecutor::Init() { 
    table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->table_oid_);
    table_iterator_.emplace(table_info_->table_->MakeIterator());
}

auto IndexScanExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                             size_t batch_size) -> bool {
    tuple_batch->clear();
    rid_batch->clear();
    tuple_batch->reserve(batch_size);
    rid_batch->reserve(batch_size);


}

}  // namespace bustub
