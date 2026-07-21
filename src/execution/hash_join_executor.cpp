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
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <vector>
#include "binder/table_ref/bound_join_ref.h"
#include "catalog/schema.h"
#include "common/config.h"
#include "common/macros.h"
#include "common/rid.h"
#include "common/util/hash_util.h"
#include "execution/expressions/abstract_expression.h"
#include "storage/page/intermediate_result_page.h"
#include "storage/page/page_guard.h"
#include "storage/table/tuple.h"
#include "type/value.h"
#include "type/value_factory.h"

namespace bustub {

namespace {
/** @brief bundles one side of the join (left or right) so Init() can loop over both instead of duplicating calls */
struct JoinSide {
  AbstractExecutor &child;
  const std::vector<AbstractExpressionRef> &key_exprs;
  std::vector<std::vector<page_id_t>> &partitions;
  std::vector<int> &tuple_count;
};
}  // namespace

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

  left_partitions_.clear();
  right_partitions_.clear();

  right_partition_tuple_count_.clear();
  left_partition_tuple_count_.clear();
  right_partition_tuple_count_.assign(NUM_PARTITIONS, 0);
  left_partition_tuple_count_.assign(NUM_PARTITIONS, 0);

  left_partition_bucket_index_ = -1;
  left_partition_page_index_ = -1;
  left_partition_tuple_index_ = -1;
  left_partition_page_size_ = -1;

  cached_right_tuples_.clear();

  const std::array<JoinSide, 2> sides{
      JoinSide{*left_child_, plan_->LeftJoinKeyExpressions(), left_partitions_, left_partition_tuple_count_},
      JoinSide{*right_child_, plan_->RightJoinKeyExpressions(), right_partitions_, right_partition_tuple_count_},
  };

  for (const auto &side : sides) {
    InitHashPages(side.child, side.key_exprs, side.partitions, side.tuple_count);
  }

  uint32_t repartition_salt = 1;
  while (true) {
    const auto indexes_repartition = GetIndexesToRepartition(right_partition_tuple_count_);
    if (indexes_repartition.empty()) {
      break;
    }

    // buffer up the left and right partitions
    for (uint16_t i = 0; i < NUM_PARTITIONS; i += 1) {
      left_partitions_.emplace_back(std::vector<page_id_t>{});
      right_partitions_.emplace_back(std::vector<page_id_t>{});

      left_partition_tuple_count_.emplace_back(0);
      right_partition_tuple_count_.emplace_back(0);
    }

    for (const auto &i : indexes_repartition) {
      for (const auto &side : sides) {
        RehashPartiton(side.partitions, i, repartition_salt, side.child.GetOutputSchema(), side.key_exprs,
                       side.tuple_count);
      }
    }

    repartition_salt += 1;
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
  tuple_batch->clear();
  rid_batch->clear();
  tuple_batch->reserve(batch_size);
  rid_batch->reserve(batch_size);

  while (batch_size > 0) {
    if (!AdvanceToNextBucketIfNeeded()) {
      // every partition bucket has been fully drained
      break;
    }

    auto left_tuple = TryFetchCurrentLeftTuple();
    if (!left_tuple.has_value()) {
      // landed on an empty left page; already advanced past it, try again
      continue;
    }

    EmitMatchesForLeftTuple(*left_tuple, tuple_batch, rid_batch, &batch_size);
  }

  return !tuple_batch->empty();
}

auto HashJoinExecutor::AdvanceToNextBucketIfNeeded() -> bool {
  // short-circuit: left_partitions_[left_partition_bucket_index_] is only evaluated once bucket_index_ != -1
  const bool current_bucket_exhausted =
      left_partition_bucket_index_ == -1 ||
      (left_partition_tuple_index_ >= left_partition_page_size_ &&
       static_cast<size_t>(left_partition_page_index_ + 1) >= left_partitions_[left_partition_bucket_index_].size());

  if (!current_bucket_exhausted) {
    return true;
  }

  if (static_cast<size_t>(left_partition_bucket_index_ + 1) >= left_partitions_.size()) {
    // no more buckets left, fully done streaming the left side
    return false;
  }

  left_partition_bucket_index_ += 1;
  BuildRightTuplesForCurrentBucket();

  left_partition_page_index_ = 0;
  left_partition_tuple_index_ = 0;
  left_partition_page_size_ = -1;
  right_tuple_matched_index_ = 0;

  return true;
}

void HashJoinExecutor::BuildRightTuplesForCurrentBucket() {
  auto *bpm = exec_ctx_->GetBufferPoolManager();

  cached_right_tuples_.clear();
  for (const auto &pid : right_partitions_[left_partition_bucket_index_]) {
    {
      auto read_guard = bpm->ReadPage(pid);
      const auto *p = read_guard.As<const IntermediateResultPage>();
      for (uint16_t i = 0; i < p->GetNumTuples(); i += 1) {
        auto hash_key =
            MakeHashKey(p->GetTupleAtIndex(i), right_child_->GetOutputSchema(), plan_->RightJoinKeyExpressions());
        cached_right_tuples_[hash_key].emplace_back(p->GetTupleAtIndex(i));
      }
    }  // read guard should have been destroyed here
    bpm->DeletePage(pid);
  }
}

auto HashJoinExecutor::TryFetchCurrentLeftTuple() -> std::optional<Tuple> {
  auto *bpm = exec_ctx_->GetBufferPoolManager();

  // check if we still have tuples in the current page to stream
  if (left_partition_page_size_ != -1 && left_partition_tuple_index_ >= left_partition_page_size_) {
    left_partition_page_size_ = -1;
    left_partition_tuple_index_ = -1;
    left_partition_page_index_ += 1;
  }

  auto read_guard = bpm->ReadPage(left_partitions_[left_partition_bucket_index_][left_partition_page_index_]);
  const auto *page = read_guard.As<const IntermediateResultPage>();
  if (left_partition_page_size_ == -1) {
    left_partition_page_size_ = page->GetNumTuples();
    left_partition_tuple_index_ = 0;
  }

  if (left_partition_page_size_ == 0) {
    // empty page, move up and let the caller retry
    left_partition_page_size_ = -1;
    left_partition_tuple_index_ = -1;
    left_partition_page_index_ += 1;
    return std::nullopt;
  }

  return page->GetTupleAtIndex(left_partition_tuple_index_);
}

void HashJoinExecutor::EmitMatchesForLeftTuple(const Tuple &left_tuple, std::vector<Tuple> *tuple_batch,
                                               std::vector<RID> *rid_batch, size_t *batch_size) {
  const auto left_hash_key = MakeHashKey(left_tuple, left_child_->GetOutputSchema(), plan_->LeftJoinKeyExpressions());

  if (auto it = cached_right_tuples_.find(left_hash_key); it != cached_right_tuples_.end()) {
    const auto &right_tuples = it->second;
    while (*batch_size > 0 && static_cast<size_t>(right_tuple_matched_index_) < right_tuples.size()) {
      tuple_batch->emplace_back(MakeOutputTuple(left_tuple, &right_tuples[right_tuple_matched_index_]));
      rid_batch->emplace_back(RID{});
      right_tuple_matched_index_ += 1;
      *batch_size -= 1;

      if (static_cast<size_t>(right_tuple_matched_index_) >= right_tuples.size()) {
        // finished processing this left tuple's matches, move on
        left_partition_tuple_index_ += 1;
        right_tuple_matched_index_ = 0;
        break;
      }
    }
  } else if (plan_->GetJoinType() == JoinType::LEFT) {
    tuple_batch->emplace_back(MakeOutputTuple(left_tuple, nullptr));
    rid_batch->emplace_back(RID{});
    *batch_size -= 1;
    left_partition_tuple_index_ += 1;
    right_tuple_matched_index_ = 0;
  } else {
    left_partition_tuple_index_ += 1;
    right_tuple_matched_index_ = 0;
  }
}

void HashJoinExecutor::InitHashPages(AbstractExecutor &child, const std::vector<AbstractExpressionRef> &child_key_exprs,
                                     std::vector<std::vector<page_id_t>> &child_hash_pages,
                                     std::vector<int> &partition_tuple_count) {
  auto *bpm = exec_ctx_->GetBufferPoolManager();
  // reset and init the child_hash_pages
  child.Init();
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

  while (child.Next(&tuples, &rids, BUSTUB_BATCH_SIZE)) {
    for (const auto &tuple : tuples) {
      auto hash_key = MakeHashKey(tuple, child.GetOutputSchema(), child_key_exprs);
      auto partition_bucket_index = GetHashPartitionIndex(hash_key, 0);

      InsertTupleIntoPartition(tuple, child_hash_pages, partition_bucket_index, partition_tuple_count);
    }
  }
}

auto HashJoinExecutor::MakeHashKey(const Tuple &tuple, const Schema &tuple_schema,
                                   const std::vector<AbstractExpressionRef> &key_exprs) const -> HashKey {
  auto keys = std::vector<Value>{};

  for (const auto &expr : key_exprs) {
    keys.emplace_back(expr->Evaluate(&tuple, tuple_schema));
  }

  return HashKey{.key_values_ = std::move(keys)};
}

auto HashJoinExecutor::GetHashPartitionIndex(const HashKey &key, const uint32_t salt) const -> size_t {
  return bustub::HashUtil::CombineHashes(std::hash<HashKey>{}(key), salt) % NUM_PARTITIONS;
}

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
      auto t = p->GetTupleAtIndex(i);
      auto hash_key = MakeHashKey(t, tuple_schema, key_exprs);
      auto partition_index = GetHashPartitionIndex(hash_key, salt);

      // recalulate the index since this is repartition, append only
      if (salt > 0) {
        partition_index += NUM_PARTITIONS * salt;
      }

      InsertTupleIntoPartition(t, partitions, partition_index, partition_tuple_count);
    }
  }

  // delete all the old pids
  for (const auto &pid : pids) {
    bpm->DeletePage(pid);
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
}

auto HashJoinExecutor::GetIndexesToRepartition(const std::vector<int> &partition_tuple_count) -> std::vector<int> {
  auto result = std::vector<int>{};

  for (size_t i = 0; i < partition_tuple_count.size(); i += 1) {
    if (partition_tuple_count[i] > COUNT_LIMIT_FOR_TUPLES_PARTITION) {
      result.emplace_back(i);
    }
  }

  return result;
}

auto HashJoinExecutor::MakeOutputTuple(const Tuple &left_tuple, const Tuple *right_tuple) const -> Tuple {
  const auto &left_tuple_schema = left_child_->GetOutputSchema();
  const auto &right_tuple_schema = right_child_->GetOutputSchema();

  auto values = std::vector<Value>{};
  values.reserve(plan_->OutputSchema().GetColumnCount());

  for (size_t i = 0; i < left_tuple_schema.GetColumnCount(); i += 1) {
    values.emplace_back(left_tuple.GetValue(&left_tuple_schema, i));
  }
  for (size_t i = 0; i < right_tuple_schema.GetColumnCount(); i += 1) {
    if (right_tuple != nullptr) {
      values.emplace_back(right_tuple->GetValue(&right_tuple_schema, i));
    } else {
      values.emplace_back(ValueFactory::GetNullValueByType(right_tuple_schema.GetColumn(i).GetType()));
    }
  }

  return Tuple{std::move(values), &plan_->OutputSchema()};
}
}  // namespace bustub
