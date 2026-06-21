//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// insert_executor.cpp
//
// Identification: src/execution/insert_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <memory>
#include <utility>
#include <vector>
#include "common/macros.h"
#include "common/rid.h"
#include "storage/table/tuple.h"
#include "type/type_id.h"
#include "type/value.h"

#include "execution/executors/insert_executor.h"

namespace bustub {

/**
 * Construct a new InsertExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The insert plan to be executed
 * @param child_executor The child executor from which inserted tuples are pulled
 */
InsertExecutor::InsertExecutor(ExecutorContext *exec_ctx, const InsertPlanNode *plan,
                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

/** Initialize the insert */
void InsertExecutor::Init() {
  has_returned_ = false;
  table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->table_oid_);
  child_executor_->Init();
}

/**
 * Yield the number of rows inserted into the table.
 * @param[out] tuple_batch The tuple batch with one integer indicating the number of rows inserted into the table
 * @param[out] rid_batch The next tuple RID batch produced by the insert (ignore, not used)
 * @param batch_size The number of tuples to be included in the batch (default: BUSTUB_BATCH_SIZE)
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 *
 * NOTE: InsertExecutor::Next() does not use the `rid_batch` out-parameter.
 * NOTE: InsertExecutor::Next() returns true with the number of inserted rows produced only once.
 */
auto InsertExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                          size_t batch_size) -> bool {
  if (has_returned_) {
    return false;
  }
  has_returned_ = true;
  tuple_batch->clear();
  rid_batch->clear();
  int inserted_tuple_count = 0;

  auto child_tuple_batch = std::vector<bustub::Tuple>{};
  auto child_rid_batch = std::vector<bustub::RID>{};

  auto table_indexes = exec_ctx_->GetCatalog()->GetTableIndexes(table_info_->name_);
  while (child_executor_->Next(&child_tuple_batch, &child_rid_batch, batch_size)) {
    for (const auto &child_tuple : child_tuple_batch) {
      auto child_tuple_meta = TupleMeta{.ts_ = 0, .is_deleted_ = false};
      auto child_rid = table_info_->table_->InsertTuple(child_tuple_meta, child_tuple, exec_ctx_->GetLockManager(),
                                                        exec_ctx_->GetTransaction(), plan_->table_oid_);
      for (const auto &index_info : table_indexes) {
        auto index_key =
            child_tuple.KeyFromTuple(table_info_->schema_, index_info->key_schema_, index_info->index_->GetKeyAttrs());
        index_info->index_->InsertEntry(index_key, child_rid.value(), exec_ctx_->GetTransaction());
      }
      inserted_tuple_count += 1;
    }
  }

  tuple_batch->emplace_back(
      bustub::Tuple(std::vector<Value>{Value(TypeId::INTEGER, inserted_tuple_count)}, &GetOutputSchema()));
  return true;
}

}  // namespace bustub
