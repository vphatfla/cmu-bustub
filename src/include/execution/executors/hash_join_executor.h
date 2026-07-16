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

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "catalog/schema.h"
#include "common/config.h"
#include "common/util/hash_util.h"
#include "execution/executor_context.h"
#include "execution/executors/abstract_executor.h"
#include "execution/expressions/abstract_expression.h"
#include "execution/plans/hash_join_plan.h"
#include "storage/table/tuple.h"
#include "type/type.h"
#include "type/value.h"

namespace bustub {

struct HashKey {
  // keys that are used to hash in HashJoinExecutor from the tuple
  std::vector<Value> key_values_;

  auto operator==(const HashKey &other) -> bool {
    if (key_values_.size() != other.key_values_.size()) {
      return false;
    }

    if (key_values_.empty()) {
      return true;
    }

    for (size_t i = 0; i < key_values_.size(); i += 1) {
      if (key_values_[i].IsNull() || other.key_values_[i].IsNull()) {
        return false;
      }

      if (auto cmp = key_values_[i].CompareEquals(other.key_values_[i]); cmp != CmpBool::CmpTrue) {
        return false;
      }
    }

    return true;
  }
};

}  // namespace bustub

namespace std {

template <>
struct hash<bustub::HashKey> {
  auto operator()(const bustub::HashKey &key) -> std::size_t {
    size_t hash_result = 0;
    for (const auto &k : key.key_values_) {
      if (!k.IsNull()) {
        hash_result = bustub::HashUtil::CombineHashes(hash_result, bustub::HashUtil::HashValue(&k));
      }
    }

    return hash_result;
  }
};
}  // namespace std

namespace bustub {

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
  static constexpr uint16_t COUNT_LIMIT_FOR_TUPLES_PARTITION = 4096;

  // constant number of partitions that we allow for both table tuples
  static constexpr uint16_t NUM_PARTITIONS = 8;

 private:
  /** The HashJoin plan node to be executed. */
  const HashJoinPlanNode *plan_;

  std::unique_ptr<AbstractExecutor> left_child_;
  std::unique_ptr<AbstractExecutor> right_child_;

  // partitions from two table
  // left_pages[i] can contain 1 or more page, the first layer (row) of this 2-d vector is the parititon
  // the 2nd layer (col) of this 2-d vector is the list of pages that share the same hashed key (in the same partition),
  // from both pages
  std::vector<std::vector<page_id_t>> left_hash_pages_;
  std::vector<std::vector<page_id_t>> right_hash_pages_;

  // memory tracker for the right partitions, incase we need to rehash
  std::vector<int> right_partition_tuple_count_;
  std::vector<int> left_partition_tuple_count_;
  // runtime hashmap for the right child tuples (since we only handle left join and inner join)
  // this is per partition not the whole right table
  std::unordered_map<HashKey, std::vector<Tuple>> right_tuples_;

  /* tracker for stream left parition left_hash_pages_[x] next() */
  // @brief the index of the partition in left_hash_pages_ that we are tracking
  uint16_t curr_left_partition_index_;
  // @brief the current tuple index to be streamed next within left_hash_pages_[curr_left_partition_index_]
  uint16_t curr_left_tuple_index_;

  // @brief helper func to repartition the tuples in the particular index from both left_hash_pages_ and
  // right_hash_pages_
  void RecursivePartitionTuples(uint16_t index);

  // @brief helper func to build the hash pages vector
  void InitHashPages(const std::unique_ptr<AbstractExecutor> &child,
                     const std::vector<AbstractExpressionRef> &child_key_exprs,
                     std::vector<std::vector<page_id_t>> &child_hash_pages, std::vector<int> &partition_tuple_count);

  // @brief helper to make the hash key from the tuples
  auto MakeHashKey(const Tuple &tuple, const Schema &tuple_schema,
                   const std::vector<AbstractExpressionRef> &key_exprs) const -> HashKey;

  // @brief helper to get the partition index given the hashkey and salt level
  auto GetHashPartitionIndex(const HashKey &key, const uint32_t salt) const -> size_t;

  // @brief helper to rehash tuple in partition
  void RehashPartiton(std::vector<std::vector<page_id_t>> &partitions, const size_t index, const uint32_t salt,
                      const Schema &tuple_schema, const std::vector<AbstractExpressionRef> &key_exprs,
                      std::vector<int> &partition_tuple_count);

  // @brief helper to insert a tuple into a page within a partition
  void InsertTupleIntoPartition(const Tuple &tuple, std::vector<std::vector<page_id_t>> &partitions,
                                const size_t partition_index, std::vector<int> &partition_tuple_count);

  // @brief helper to check if the partitions need rehasing/repartition
  auto GetIndexesToRepartition(const std::vector<int> &partition_tuple_count) -> std::vector<int>;
};

}  // namespace bustub
