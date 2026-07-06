//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// nested_index_join_executor.cpp
//
// Identification: src/execution/nested_index_join_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/nested_index_join_executor.h"
#include <utility>
#include <vector>
#include "binder/table_ref/bound_join_ref.h"
#include "common/config.h"
#include "common/macros.h"
#include "common/rid.h"
#include "storage/table/tuple.h"
#include "type/value.h"
#include "type/value_factory.h"

namespace bustub {

/**
 * Creates a new nested index join executor.
 * @param exec_ctx the context that the nested index join should be performed in
 * @param plan the nested index join plan to be executed
 * @param child_executor the outer table
 */
NestedIndexJoinExecutor::NestedIndexJoinExecutor(ExecutorContext *exec_ctx, const NestedIndexJoinPlanNode *plan,
                                                 std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {
  if (plan->GetJoinType() != JoinType::LEFT && plan->GetJoinType() != JoinType::INNER) {
    // Note for Spring 2025: You ONLY need to implement left join and inner join.
    throw bustub::NotImplementedException(fmt::format("join type {} not supported", plan->GetJoinType()));
  }
}

void NestedIndexJoinExecutor::Init() {
  child_executor_->Init();
  outter_tuples_.clear();
  outter_rids_.clear();
  outter_tuple_pos_ = 0;

  inner_table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->inner_table_oid_);
  inner_index_info_ = exec_ctx_->GetCatalog()->GetIndex(plan_->index_oid_);
}

auto NestedIndexJoinExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                                   size_t batch_size) -> bool {
  tuple_batch->clear();
  rid_batch->clear();
  tuple_batch->reserve(batch_size);
  rid_batch->reserve(batch_size);

  while (batch_size > 0) {
    if (outter_tuple_pos_ >= outter_tuples_.size()) {
      outter_tuples_.clear();
      outter_rids_.clear();
      outter_tuples_.reserve(BUSTUB_BATCH_SIZE);
      outter_rids_.reserve(BUSTUB_BATCH_SIZE);

      // refresh the outter tupple buffer
      if (!child_executor_->Next(&outter_tuples_, &outter_rids_, BUSTUB_BATCH_SIZE)) {
        // no more outter tuple, next is now exhausted
        break;
      }

      outter_tuple_pos_ = 0;
      did_outter_tuple_match_ = false;
      should_fetch_inner_rids_ = true;
    }
    auto outter_tuple = outter_tuples_[outter_tuple_pos_];

    if (should_fetch_inner_rids_) {
      inner_rid_pos_ = 0;
      inner_rids_.clear();
      // evaluate the index predicate against the outter tuple, get the neccessary columns from outter tuple to form the
      // value
      auto index_value = plan_->KeyPredicate()->Evaluate(&outter_tuple, child_executor_->GetOutputSchema());
      // construct the index tuple that can be used to query the ric
      auto index_tuple = Tuple{std::vector<Value>{index_value}, &inner_index_info_->key_schema_};
      inner_index_info_->index_->ScanKey(index_tuple, &inner_rids_, exec_ctx_->GetTransaction());
      should_fetch_inner_rids_ = false;
      did_outter_tuple_match_ = false;
    }

    while (inner_rid_pos_ < inner_rids_.size() && batch_size > 0) {
      // current outter tuple still have left over in the buffer since the previous Next()
      auto inner_rid = inner_rids_[inner_rid_pos_];
      auto [t_meta, t] = inner_table_info_->table_->GetTuple(inner_rid);
      if (!t_meta.is_deleted_) {
        tuple_batch->emplace_back(ConstructOutputTuple(outter_tuple, &t));
        rid_batch->emplace_back(RID{});

        batch_size -= 1;
        did_outter_tuple_match_ = true;
      }
      inner_rid_pos_ += 1;
    }

    if (inner_rid_pos_ >= inner_rids_.size() && !did_outter_tuple_match_ && plan_->GetJoinType() == JoinType::LEFT) {
      tuple_batch->emplace_back(ConstructOutputTuple(outter_tuple, nullptr));
      rid_batch->emplace_back(RID{});
      batch_size -= 1;
    }

    if (inner_rid_pos_ >= inner_rids_.size()) {
      outter_tuple_pos_ += 1;
      should_fetch_inner_rids_ = true;
    }
  }

  return !tuple_batch->empty();
}

auto NestedIndexJoinExecutor::ConstructOutputTuple(const Tuple &left, const Tuple *right) -> Tuple {
  auto values = std::vector<Value>{};
  values.reserve(GetOutputSchema().GetColumnCount());

  for (unsigned int i = 0; i < child_executor_->GetOutputSchema().GetColumnCount(); i += 1) {
    values.emplace_back(left.GetValue(&child_executor_->GetOutputSchema(), i));
  }

  for (unsigned int i = 0; i < inner_table_info_->schema_.GetColumnCount(); i += 1) {
    if (right != nullptr) {
      values.emplace_back(right->GetValue(&inner_table_info_->schema_, i));
    } else {
      values.emplace_back(ValueFactory::GetNullValueByType(inner_table_info_->schema_.GetColumn(i).GetType()));
    }
  }

  return Tuple{values, &GetOutputSchema()};
}

}  // namespace bustub
