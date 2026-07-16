//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// hash_join_executor.cpp
//
// Identification: src/execution/hash_join_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/hash_join_executor.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>
#include "catalog/schema.h"
#include "common/config.h"
#include "common/macros.h"
#include "common/util/hash_util.h"
#include "execution/expressions/abstract_expression.h"
#include "storage/page/intermediate_result_page.h"
#include "storage/page/page_guard.h"
#include "storage/table/tuple.h"

namespace bustub {

/**
 * Construct a new HashJoinExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The HashJoin join plan to be executed
 * @param left_child The child executor that produces tuples for the left side of join
 * @param right_child The child executor that produces tuples for the right side of join
 */
HashJoinExecutor::HashJoinExecutor(ExecutorContext *exec_ctx, const HashJoinPlanNode *plan,
                                   std::unique_ptr<AbstractExecutor> &&left_child,
                                   std::unique_ptr<AbstractExecutor> &&right_child)
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      left_child_(std::move(left_child)),
      right_child_(std::move(right_child)) {
  if (plan->GetJoinType() != JoinType::LEFT && plan->GetJoinType() != JoinType::INNER) {
    // Note for Spring 2025: You ONLY need to implement left join and inner join.
    throw bustub::NotImplementedException(fmt::format("join type {} not supported", plan->GetJoinType()));
  }
}

/** Initialize the join */
void HashJoinExecutor::Init() {
  // pipeline breaker, load all tuples from both children on init
  left_child_->Init();
  right_child_->Init();

  left_hash_pages_.clear();
  right_hash_pages_.clear();

  right_partition_tuple_count_.clear();
  left_partition_tuple_count_.clear();
  right_partition_tuple_count_.assign(NUM_PARTITIONS, 0);
  left_partition_tuple_count_.assign(NUM_PARTITIONS, 0);

  InitHashPages(left_child_, plan_->LeftJoinKeyExpressions(), left_hash_pages_, left_partition_tuple_count_);
  InitHashPages(right_child_, plan_->RightJoinKeyExpressions(), right_hash_pages_, right_partition_tuple_count_);

  while (true) {
    const auto indexes_repartition = GetIndexesToRepartition(right_partition_tuple_count_);
    if (indexes_repartition.empty()) {
      break;
    }

    for (const auto &i : indexes_repartition) {
      RehashPartiton(right_hash_pages_, i, 1, right_child_->GetOutputSchema(), plan_->LeftJoinKeyExpressions(),
                     left_partition_tuple_count_);
      RehashPartiton(left_hash_pages_, i, 1, left_child_->GetOutputSchema(), plan_->RightJoinKeyExpressions(),
                     left_partition_tuple_count_);

      // todo: determine how to increase the salt and when
    }
  }
}

/**
 * Yield the next tuple batch from the hash join.
 * @param[out] tuple_batch The next tuple batch produced by the hash join
 * @param[out] rid_batch The next tuple RID batch produced by the hash join
 * @param batch_size The number of tuples to be included in the batch (default: BUSTUB_BATCH_SIZE)
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 */
auto HashJoinExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                            size_t batch_size) -> bool {
  UNIMPLEMENTED("TODO(P3): Add implementation.");
}

void HashJoinExecutor::RecursivePartitionTuples(uint16_t index) {}

void HashJoinExecutor::InitHashPages(const std::unique_ptr<AbstractExecutor> &child,
                                     const std::vector<AbstractExpressionRef> &child_key_exprs,
                                     std::vector<std::vector<page_id_t>> &child_hash_pages,
                                     std::vector<int> &partition_tuple_count) {
  auto *bpm = exec_ctx_->GetBufferPoolManager();
  // reset and init the child_hash_pages
  child->Init();
  child_hash_pages.clear();
  child_hash_pages.assign(NUM_PARTITIONS, {});

  for (uint16_t i = 0; i < NUM_PARTITIONS; i += 1) {
    auto pid = bpm->NewPage();
    auto write_page_guard = bpm->WritePage(pid);

    auto page = write_page_guard.AsMut<IntermediateResultPage>();
    page->Init();

    child_hash_pages[i].emplace_back(pid);
  }

  auto tuples = std::vector<Tuple>{};
  auto rids = std::vector<RID>{};

  while (child->Next(&tuples, &rids, BUSTUB_BATCH_SIZE)) {
    for (const auto &tuple : tuples) {
      auto hashKey = MakeHashKey(tuple, child->GetOutputSchema(), child_key_exprs);
      auto partition_bucket_index = GetHashPartitionIndex(hashKey, 0);

      InsertTupleIntoPartition(tuple, child_hash_pages, partition_bucket_index, partition_tuple_count);
    }
  }
};

auto HashJoinExecutor::MakeHashKey(const Tuple &tuple, const Schema &tuple_schema,
                                   const std::vector<AbstractExpressionRef> &key_exprs) const -> HashKey {
  auto keys = std::vector<Value>{};

  for (const auto &expr : key_exprs) {
    keys.emplace_back(expr->Evaluate(&tuple, tuple_schema));
  }

  return HashKey{.key_values_ = std::move(keys)};
};

auto HashJoinExecutor::GetHashPartitionIndex(const HashKey &key, const uint32_t salt) const -> size_t {
  return bustub::HashUtil::CombineHashes(std::hash<HashKey>{}(key), salt) % NUM_PARTITIONS;
};

void HashJoinExecutor::RehashPartiton(std::vector<std::vector<page_id_t>> &partitions, const size_t index,
                                      const uint32_t salt, const Schema &tuple_schema,
                                      const std::vector<AbstractExpressionRef> &key_exprs,
                                      std::vector<int> &partition_tuple_count) {
  auto *bpm = exec_ctx_->GetBufferPoolManager();

  auto pids = std::move(partitions[index]);
  // remove all the pids from the partitions
  partitions[index] = std::vector<page_id_t>{};
  partition_tuple_count[index] = 0;

  for (const auto &pid : pids) {
    auto write_page_guard = bpm->WritePage(pid);
    const auto *p = write_page_guard.AsMut<const IntermediateResultPage>();

    auto num_tuples = p->GetNumTuples();
    for (uint16_t i = 0; i < num_tuples; i += 1) {
      auto t = p->GetTuple(i);
      auto hash_key = MakeHashKey(t, tuple_schema, key_exprs);
      auto partition_index = GetHashPartitionIndex(hash_key, salt);
      InsertTupleIntoPartition(t, partitions, partition_index, partition_tuple_count);
    }
  }
}

void HashJoinExecutor::InsertTupleIntoPartition(const Tuple &tuple, std::vector<std::vector<page_id_t>> &partitions,
                                                const size_t partition_index, std::vector<int> &partition_tuple_count) {
  auto *bpm = exec_ctx_->GetBufferPoolManager();

  auto &pids = partitions[partition_index];
  page_id_t last_pid;
  if (pids.empty()) {
    last_pid = bpm->NewPage();
  } else {
    last_pid = pids[pids.size() - 1];
  }
  auto write_page_guard = bpm->WritePage(last_pid);
  auto *p = write_page_guard.AsMut<IntermediateResultPage>();
  if (pids.empty()) {
    p->Init();
    pids.emplace_back(last_pid);
  }

  if (auto insert_status = p->InsertTuple(tuple); !insert_status) {
    // page is full, need to create new page
    auto pid = bpm->NewPage();
    auto write_page_guard = bpm->WritePage(pid);

    auto *p = write_page_guard.AsMut<IntermediateResultPage>();
    p->Init();

    BUSTUB_ASSERT(p->InsertTuple(tuple), "new page insert must success");

    pids.emplace_back(pid);
  }

  partition_tuple_count[partition_index] += 1;
};

auto HashJoinExecutor::GetIndexesToRepartition(const std::vector<int> &partition_tuple_count) -> std::vector<int> {
  auto result = std::vector<int>{};

  for (size_t i = 0; i < NUM_PARTITIONS; i += 1) {
    if (partition_tuple_count[i] > COUNT_LIMIT_FOR_TUPLES_PARTITION) {
      result.emplace_back(i);
    }
  }

  return result;
};
}  // namespace bustub
