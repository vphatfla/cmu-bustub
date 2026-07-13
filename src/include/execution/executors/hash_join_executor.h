//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// hash_join_executor.h
//
// Identification: src/include/execution/executors/hash_join_executor.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "common/config.h"
#include "execution/executor_context.h"
#include "execution/executors/abstract_executor.h"
#include "execution/plans/hash_join_plan.h"
#include "storage/table/tuple.h"
#include "type/value.h"

namespace bustub {

struct HashKey {
  // keys that are used to hash in HashJoinExecutor from the tuple
  std::vector<Value> key_values_;
  // TODO operator==
};

/**
 * HashJoinExecutor executes a nested-loop JOIN on two tables.
 */
class HashJoinExecutor : public AbstractExecutor {
 public:
  HashJoinExecutor(ExecutorContext *exec_ctx, const HashJoinPlanNode *plan,
                   std::unique_ptr<AbstractExecutor> &&left_child, std::unique_ptr<AbstractExecutor> &&right_child);

  void Init() override;

  auto Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch, size_t batch_size)
      -> bool override;

  /** @return The output schema for the join */
  auto GetOutputSchema() const -> const Schema & override { return plan_->OutputSchema(); };

  /* memory limit for the unordered_map tuples from the right table partitions in mem at once, 4KB */
  static constexpr uint16_t MEM_LIMIT_FOR_TUPLES_PARTITION = 4906;

 private:
  /** The HashJoin plan node to be executed. */
  const HashJoinPlanNode *plan_;

  std::unique_ptr<AbstractExecutor> left_child_;
  std::unique_ptr<AbstractExecutor> right_child_;

  // partitions from two table
  // left_pages[i] can contain 1 or more page, the first layer (row) of this 2-d vector is the parititon
  // the 2nd layer (col) of this 2-d vector is the list of pages that share the same hashed key (in the same partition),
  // from both pages
  std::vector<std::vector<page_id_t>> left_pages_;
  std::vector<std::vector<page_id_t>> right_pages_;

  // runtime hashmap for the right child tuples (since we only handle left join and inner join)
  // this is per partition not the whole right table
  std::unordered_map<HashKey, std::vector<Tuple>> right_tuples_;
  /* tracker for stream next() */
};

}  // namespace bustub
