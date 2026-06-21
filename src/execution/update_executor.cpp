//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// update_executor.cpp
//
// Identification: src/execution/update_executor.cpp
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

#include "execution/executors/update_executor.h"

namespace bustub {

/**
 * Construct a new UpdateExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The update plan to be executed
 * @param child_executor The child executor that feeds the update
 */
UpdateExecutor::UpdateExecutor(ExecutorContext *exec_ctx, const UpdatePlanNode *plan,
                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

/** Initialize the update */
void UpdateExecutor::Init() {
  has_returned_ = false;
  table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->table_oid_);
  child_executor_->Init();
}

/**
 * Yield the number of rows updated in the table.
 * @param[out] tuple_batch The tuple batch with one integer indicating the number of rows updated in the table
 * @param[out] rid_batch The next tuple RID batch produced by the update (ignore, not used)
 * @param batch_size The number of tuples to be included in the batch (default: BUSTUB_BATCH_SIZE)
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 *
 * NOTE: UpdateExecutor::Next() does not use the `rid_batch` out-parameter.
 * NOTE: UpdateExecutor::Next() returns true with the number of updated rows produced only once.
 */
auto UpdateExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                          size_t batch_size) -> bool {
  tuple_batch->clear();
  rid_batch->clear();
  if (has_returned_) {
    return false;
  }
  has_returned_ = true;

  int updated_tuple_count = 0;

  auto child_tuple_batch = std::vector<bustub::Tuple>{};
  auto child_rid_batch = std::vector<bustub::RID>{};

  auto table_indexes = exec_ctx_->GetCatalog()->GetTableIndexes(table_info_->name_);
  while (child_executor_->Next(&child_tuple_batch, &child_rid_batch, batch_size)) {
    for (const auto &old_tuple : child_tuple_batch) {
      // mark the tuple as delete in the current table first
      table_info_->table_->UpdateTupleMeta({.ts_ = 0, .is_deleted_ = true}, old_tuple.GetRid());

      // compute new tuple with plan experission
      auto new_values = std::vector<Value>{};
      for (const auto &expr : plan_->target_expressions_) {
        new_values.emplace_back(expr->Evaluate(&old_tuple, table_info_->schema_));
      }
      auto new_tuple = bustub::Tuple{new_values, &table_info_->schema_};

      // insert the new tuple
      auto new_tuple_rid = table_info_->table_->InsertTuple(TupleMeta{.ts_ = 0, .is_deleted_ = false}, new_tuple);

      for (const auto &index_info : table_indexes) {
        // remove the old indexes
        auto old_index_key =
            old_tuple.KeyFromTuple(table_info_->schema_, index_info->key_schema_, index_info->index_->GetKeyAttrs());
        index_info->index_->DeleteEntry(old_index_key, old_tuple.GetRid(), exec_ctx_->GetTransaction());
        // add the index for the new tuple
        auto new_index_key =
            new_tuple.KeyFromTuple(table_info_->schema_, index_info->key_schema_, index_info->index_->GetKeyAttrs());
        index_info->index_->InsertEntry(new_index_key, new_tuple_rid.value(), exec_ctx_->GetTransaction());
      }
      updated_tuple_count += 1;
    }
  }

  tuple_batch->emplace_back(
      bustub::Tuple(std::vector<Value>{Value{TypeId::INTEGER, updated_tuple_count}}, &GetOutputSchema()));
  return true;
}

}  // namespace bustub
