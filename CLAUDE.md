# BusTub Working Context

> **CLAUDE CODE DIRECTIVE:** Automatically update this context file as you work. Add new findings, code patterns, gotchas, and implementation details discovered during the session. Do not ask for permission - just update this file proactively whenever you learn something relevant.
>
> **MANDATORY:** Always re-read the relevant source files (using the Read tool) before answering any question about the codebase. Never rely on previously cached file contents — the user may have edited files between questions.

## Project: CMU 15-445 - Fall 2025

---

# Project 3: Query Execution

## P3 Tasks Overview

| Task | Description | Status |
|------|-------------|--------|
| Task #1 | Access Method Executors (SeqScan, Insert, Update, Delete, IndexScan, optimizer) | ✅ DONE |
| Task #2 | Aggregation & Join Executors (Aggregation ✅, NLJ ✅, NestedIndexJoin ✅) | ✅ DONE |
| Task #3 | Hash Join & Optimization (IntermediateResultPage ✅, HashJoin ⬜, NLJ→HashJoin optimizer ⬜) | 🔶 IN PROGRESS |
| Task #4 | Sort, Limit, TopN & Window Functions (ExternalMergeSort, Limit, TopN, Sort+Limit→TopN, WindowFunction) | ⬜ TODO |

### Verified State (2026-07-08 — read from source, not cache)
- **Tasks #1 & #2**: fully implemented & verified (9 executors + SeqScan→IndexScan optimizer). Only cosmetic comment typos (`outter`, `experission`, `comparsion`, `bnreak`).
- **Task #3**: ALL stubs — `intermediate_result_page.h` (empty class), `hash_join_executor.cpp` (ctor/Init/Next `UNIMPLEMENTED`), `nlj_as_hash_join.cpp` (returns plan unchanged). `hash_join_plan.h` is READY: `LeftJoinKeyExpressions()`, `RightJoinKeyExpressions()`, `GetJoinType()`, `GetLeftPlan()`, `GetRightPlan()`.
- **Task #4**: ALL stubs — `execution_common.cpp` (`TupleComparator::operator()`→false, `GenerateSortKey()`→{}), `external_merge_sort_executor.{h,cpp}` (Iterator + ctor/Init/Next `UNIMPLEMENTED`, only `template class ...<2>` instantiated), `limit_executor.cpp`, `topn_executor.cpp` (+`GetNumInHeap`), `topn_per_group_executor.cpp`, `window_function_executor.cpp` (ctor done, Init throws, Next→false), `sort_limit_as_topn.cpp` (returns plan unchanged).

### Untracked components discovered (not in original task list)
- **TopN**: `src/execution/topn_executor.cpp` (has `GetNumInHeap()`) + `src/optimizer/sort_limit_as_topn.cpp` (Sort+Limit→TopN rule). Tested by `p3.17-topn.slt`.
- **TopNPerGroup**: `src/execution/topn_per_group_executor.cpp` (likely leaderboard/window related).
- **Full p3 test list** (28 files in `test/sql/`): p3.00–p3.13 (Tasks 1&2, passing), p3.14-hash-join, p3.15-multi-way-hash-join, p3.16-sort-limit, p3.17-topn, p3.18/19-integration, p3.20-window-function, p3.22-composite-key-index-scan, p3.leaderboard-q1/q1-index/q1-window/q2/q3.
- **Run command**: `build/bin/bustub-sqllogictest <test.slt> --verbose -d --in-memory` (build/ is configured; CMakeCache present).

---

## P3 File Locations

### Executor Files (12 stubs to implement)
| Executor | Header | Source |
|----------|--------|--------|
| SeqScan | `src/include/execution/executors/seq_scan_executor.h` | `src/execution/seq_scan_executor.cpp` |
| Insert | `src/include/execution/executors/insert_executor.h` | `src/execution/insert_executor.cpp` |
| Update | `src/include/execution/executors/update_executor.h` | `src/execution/update_executor.cpp` |
| Delete | `src/include/execution/executors/delete_executor.h` | `src/execution/delete_executor.cpp` |
| IndexScan | `src/include/execution/executors/index_scan_executor.h` | `src/execution/index_scan_executor.cpp` |
| Aggregation | `src/include/execution/executors/aggregation_executor.h` | `src/execution/aggregation_executor.cpp` |
| NestedLoopJoin | `src/include/execution/executors/nested_loop_join_executor.h` | `src/execution/nested_loop_join_executor.cpp` |
| NestedIndexJoin | `src/include/execution/executors/nested_index_join_executor.h` | `src/execution/nested_index_join_executor.cpp` |
| HashJoin | `src/include/execution/executors/hash_join_executor.h` | `src/execution/hash_join_executor.cpp` |
| ExternalMergeSort | `src/include/execution/executors/external_merge_sort_executor.h` | `src/execution/external_merge_sort_executor.cpp` |
| Limit | `src/include/execution/executors/limit_executor.h` | `src/execution/limit_executor.cpp` |
| WindowFunction | `src/include/execution/executors/window_function_executor.h` | `src/execution/window_function_executor.cpp` |

### Optimizer Rules (2 stubs to implement)
| Rule | Source |
|------|--------|
| SeqScan→IndexScan | `src/optimizer/seqscan_as_indexscan.cpp` |
| NLJ→HashJoin | `src/optimizer/nlj_as_hash_join.cpp` |

### Other P3 Files to Implement
| Component | File |
|-----------|------|
| IntermediateResultPage | `src/include/storage/page/intermediate_result_page.h` |
| TupleComparator + GenerateSortKey | `src/execution/execution_common.cpp` |

### Key Support Files (read-only, already implemented)
| File | Purpose |
|------|---------|
| `src/include/execution/executors/abstract_executor.h` | Base class: `Init()` + `Next()` batch interface |
| `src/include/execution/executor_context.h` | Access to catalog, BPM, transaction |
| `src/include/catalog/catalog.h` | `GetTable()`, `GetIndex()`, `GetTableIndexes()` |
| `src/include/execution/expressions/column_value_expression.h` | `GetTupleIdx()` (0=left, 1=right), `GetColIdx()` |
| `src/include/execution/expressions/comparison_expression.h` | ComparisonType enum, `PerformComparison()` |
| `src/include/execution/expressions/logic_expression.h` | AND/OR with 3-valued NULL logic |
| `src/include/execution/plans/aggregation_plan.h` | `AggregateKey`/`AggregateValue`, `SimpleAggregationHashTable` |
| `src/include/execution/execution_common.h` | `SortKey`, `SortEntry`, `TupleComparator` types |
| `src/include/execution/plans/*.h` | All plan node definitions |

---

## P3 Executor Architecture

### Volcano Iterator Model (Batch Processing)
All executors implement `Init()` + `Next()`:
```cpp
void Init() override;
auto Next(std::vector<Tuple> *tuple_batch, std::vector<RID> *rid_batch, size_t batch_size) -> bool override;
```
- `Next()` fills vectors up to `batch_size`, returns `false` when exhausted
- Pipeline breakers (Aggregation, HashJoin, ExternalMergeSort, WindowFunction) build in `Init()`, emit in `Next()`

### ExecutorContext Provides
- `GetCatalog()` → table/index metadata
- `GetBufferPoolManager()` → page allocation for disk spill
- `GetTransaction()` → transaction context

### Catalog API
- `GetTable(table_oid)` → `TableInfo` (schema, name, TableHeap, oid)
- `GetIndex(index_oid)` → `IndexInfo` (key_schema, Index, name)
- `GetTableIndexes(table_name)` → `vector<IndexInfo>` (all indexes on table)

### Expression Evaluation
- `expr->Evaluate(tuple, schema)` → single-tuple context
- `expr->EvaluateJoin(left_tuple, left_schema, right_tuple, right_schema)` → join context
- `ColumnValueExpression::GetTupleIdx()`: 0 = left table, 1 = right table
- `ComparisonExpression`: Equal, NotEqual, LessThan, etc.
- `LogicExpression`: AND/OR with 3-valued NULL logic (NULL AND false = false, NULL OR true = true, etc.)

---

## P3 Task Details

### Task #1: Access Method Executors
| Executor | Key Implementation Notes |
|----------|------------------------|
| **SeqScan** | Iterate `TableHeap` batch-by-batch; skip deleted tuples (`TupleMeta.is_deleted_`); apply `filter_predicate_` if present |
| **Insert** | Pull from child executor; insert into `TableHeap`; update ALL indexes via `GetTableIndexes()`; return single integer tuple (row count) |
| **Update** | Delete-then-insert strategy; update indexes; return integer count |
| **Delete** | Mark tuples deleted; update indexes; return integer count |
| **IndexScan** | Two modes: (1) point lookup via `pred_keys_` + `ScanKey`, (2) ordered scan via index iterator; cast to `BPlusTreeIndexForTwoIntegerColumn`; skip deleted |
| **SeqScan→IndexScan** | Optimizer rule: SeqScan+Filter → IndexScan when index exists; handle `v=1`, `1=v`, `v=1 OR v=4`; skip AND predicates |

**Tests:** `p3.00-primer.slt` through `p3.06-*`

### Task #2: Aggregation & Join Executors
| Executor | Key Implementation Notes |
|----------|------------------------|
| **Aggregation** | Pipeline breaker; build `SimpleAggregationHashTable` in `Init()`; implement `CombineAggregateValues`; handle NULLs; empty table → CountStar=0, others=NULL |
| **NestedLoopJoin** | Inner + left outer join; use `EvaluateJoin()` for predicate; NULL-pad unmatched left tuples for left join; 3-valued logic |
| **NestedIndexJoin** | One child (outer); probe index on inner table using `key_predicate`; fetch tuple via RID; skip deleted; inner + left outer |

**Aggregation types:** CountStar, Count, Sum, Min, Max

**Tests:** `p3.07-*` through `p3.13-*`

### Task #3: Hash Join & Optimization
| Component | Key Implementation Notes |
|-----------|------------------------|
| **IntermediateResultPage** | Custom page format for spilling tuples to disk; compact layout; variable-length tuple support; reused by ExternalMergeSort |
| **HashJoin** | Grace Hash Join — must handle data exceeding memory; spill via `IntermediateResultPage`; use `GetLeftJoinKey()`/`GetRightJoinKey()`; inner + left outer; pipeline breaker |
| **NLJ→HashJoin optimizer** | Transform NLJ → HashJoin on conjunction of equi-conditions (`col=col AND col=col AND ...`); use `GetTupleIdx()` to identify table sides; recursive extraction |

**Tests:** `p3.14-*` through `p3.15-*`

### Task #4: Sort, Limit & Window Functions
| Component | Key Implementation Notes |
|-----------|------------------------|
| **TupleComparator** | Implement `operator()` + `GenerateSortKey()` in `execution_common.cpp`; handle ASC/DESC, NULL placement |
| **ExternalMergeSort** | 2-way external merge sort; spill to `IntermediateResultPage`; delete temp pages after; `std::sort` only on single-page data; pipeline breaker |
| **Limit** | Cap output to `plan_->GetLimit()` tuples; trivial |
| **WindowFunction** | PARTITION BY + ORDER BY; frame: with ORDER BY = first-to-current, without = entire partition; reuse aggregation logic; implement RANK (with ties); types: CountStar, Count, Sum, Min, Max, Rank |

---

## P3 Implementation Order (Suggested)

```
Phase 1 (Task #1):  SeqScan → Insert → Delete → Update → IndexScan → SeqScan→IndexScan optimizer
Phase 2 (Task #2):  Aggregation → NestedLoopJoin → NestedIndexJoin
Phase 3 (Task #3):  IntermediateResultPage → HashJoin → NLJ→HashJoin optimizer
Phase 4 (Task #4):  TupleComparator → ExternalMergeSort → Limit → WindowFunction
```

---

## P3 Gotchas

- **Batch interface**: `Next()` returns vectors of tuples, not single tuples — fill up to `batch_size`
- **Index updates**: Insert/Update/Delete MUST update ALL table indexes
- **Deleted tuples**: Every scan executor must skip `TupleMeta.is_deleted_` tuples
- **NULL handling**: Aggregation, joins, and window functions must handle NULLs (3-valued logic)
- **Memory limits**: HashJoin and ExternalMergeSort must spill to disk — can't hold everything in memory
- **Page cleanup**: Delete temporary pages after ExternalMergeSort completes
- **IntermediateResultPage**: Shared design between HashJoin and ExternalMergeSort — design carefully first
- **Update = delete + insert**: Not in-place mutation
- **Aggregation empty table**: CountStar → 0, all others → integer_null
- **IndexScan cast**: Must cast to `BPlusTreeIndexForTwoIntegerColumn`

---

## P3 Testing

```bash
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(sysctl -n hw.ncpu)
# Run individual SQLLogicTests:
./bin/bustub-sqllogictest ../test/sql/p3.00-primer.slt --verbose
# Interactive shell:
./bin/bustub-shell
# EXPLAIN query plan:
# EXPLAIN (o)    — optimized plan
# EXPLAIN (o,s)  — optimized plan with schema
# Formatting (required — grade=0 if fail):
make format && make check-lint && make check-clang-tidy-p3
# Submission:
make submit-p3
```

---

## P3 Implementation Progress

### SeqScan — Done
- Members: `table_info_` (shared_ptr), `table_iterator_` (std::optional)
- `Init()`: fetch table info from catalog, create iterator
- `Next()`: skip deleted tuples, apply `filter_predicate_` if present, fill batch

### Insert — Done
- Drains child, inserts into table heap, updates all indexes, returns single count tuple
- Uses `has_returned_` flag to prevent infinite loop on second `Next()` call

### Update — Done
- Delete-then-insert strategy: mark old tuple deleted, compute new tuple via `target_expressions_`, insert new tuple
- Updates indexes: delete old key, insert new key
- Returns single count tuple with `has_returned_` flag

### Delete — Done
- Marks tuples as deleted via `UpdateTupleMeta({0, true}, rid)`, deletes index entries
- Returns single count tuple with `has_returned_` flag

### IndexScan — Done
- Members: `table_info_`, `index_info_`, `tree_` (cast to `BPlusTreeIndexForTwoIntegerColumn`), `rid_`/`rid_pos_` (point lookup), `tree_iterator_` (ordered scan)
- Two modes based on `pred_keys_`:
  - **`pred_keys_` non-empty → point lookup**: each pred_key is a separate `ScanKey` call (union/OR semantics, not intersection). Build `Tuple{{val}, &key_schema_}` per key, append RIDs to `rid_`. Track position with `rid_pos_` across `Next()` calls.
  - **`pred_keys_` empty → ordered scan**: `tree_->GetBeginIterator()`, dereference iterator with `*iter` → `pair<Key&, RID&>`, advance with `++iter`
- Skip deleted tuples (`meta.is_deleted_`) in both modes via `GetTuple(rid)` → `[meta, tuple]`
- `Next()` must loop until batch is non-empty or source exhausted (deleted tuples can cause empty batches)
- `filter_predicate_` — ignore for now; handle when implementing the optimizer rule
- No `TableIterator` needed — tuples fetched directly via `GetTuple(rid)`

### SeqScan→IndexScan Optimizer — Done
- File: `src/optimizer/seqscan_as_indexscan.cpp`
- Runs after `OptimizeMergeFilterScan` (filter already merged into SeqScan's `filter_predicate_`)
- Helper: `SplitComparisonExpr(expr)` → `optional<pair<col_expr_ref, const_expr_ref>>` — handles both `col = const` and `const = col` (flipped)
- **Case 1: `ComparisonExpression(Equal)`** → call `SplitComparisonExpr`, extract col_idx via `ColumnValueExpression::GetColIdx()`, call `MatchIndex(table_name, col_idx)`, create `IndexScanPlanNode` with one `pred_key`
- **Case 2: `LogicExpression(Or)`** → call `SplitComparisonExpr` on both children, verify same `GetColIdx()`, call `MatchIndex`, create `IndexScanPlanNode` with two `pred_keys`
- **Skip**: AND predicates, range comparisons, cross-column OR, non-equality — keep as SeqScan
- `pred_keys_` are `AbstractExpressionRef` (shared_ptr to `ConstantValueExpression`) — reuse existing shared_ptrs from the expression tree, don't construct new ones
- `MatchIndex(table_name, col_idx)` returns `optional<tuple<index_oid, index_name>>` — checks if any index has `key_attrs == {col_idx}`

### Aggregation — Done (p3.07/p3.08/p3.09 PASS)
- `CombineAggregateValues` implemented for all 5 types; NULL-aware (skip null input except CountStar).
- `Init()`: drain child in batches → `aht_.InsertCombine(MakeAggregateKey, MakeAggregateValue)` per tuple → set `aht_iterator_ = aht_.Begin()`.
- **Empty-table + no GROUP BY edge case**: after draining, `if (plan_->GetGroupBys().empty() && aht_.Begin() == aht_.End()) aht_.InsertInitial(AggregateKey{});`. Added `InsertInitial(key)` helper to `SimpleAggregationHashTable` that inserts the RAW `GenerateInitialAggregateValue()` (NOT `InsertCombine`, which would bump CountStar to 1). Handling this in `Init()` (not `Next()`) means `Next()` needs no flag — it emits the one default row exactly once, then hits `End()`.
- **GROUP BY works with zero special-casing**: `MakeAggregateKey` builds a key from all group-by exprs; empty key = whole-table aggregate; non-empty key = one bucket per group. Multi-column + NULL keys handled by `AggregateKey::operator==` and `std::hash<AggregateKey>` (NULLs group together).
- **HAVING** is a separate Filter executor above Aggregation — nothing to do in the aggregation executor.
- `Next()` emits `group_bys_ ++ aggregates_` (matches output schema column order).
- Build target is `make sqllogictest` (NOT `bustub-sqllogictest`); binary at `build/bin/bustub-sqllogictest`.

### NestedLoopJoin — Done (p3.11/p3.12 PASS; p3.10 NLJ queries + nlj_init_check PASS)
- **Design**: materialize the RIGHT (inner) side fully in `Init()`; STREAM the LEFT (outer) side one buffered batch at a time in `Next()`. Left=outer=child0=`GetLeftPlan()`, right=inner=child1=`GetRightPlan()` (fixed by planner; factory passes them in that order). NLJ is NOT graded on memory (only HashJoin/ExternalMergeSort spill), so materializing the inner is fine even if it's the larger table.
- **Init() materialization gotcha**: child `Next()` CLEARS its output vector each call, so you CANNOT accumulate by passing `right_tuples_` directly to repeated `Next()` calls (it ends up empty). Use a temp `batch_tuples`/`batch_rids` and `right_tuples_.insert(end, batch.begin(), batch.end())` each iteration.
- **Init() MUST reset** `left_pos_=0, right_pos_=0, did_left_match=false` (not just clear vectors) — else re-execution (p3.12) resumes `right_pos_` mid-scan and skips right tuples.
- **Next() resume state (members)**: `left_tuples_`+`left_pos_` (buffered left batch + index), `right_pos_` (scan position in materialized right for current left tuple), `did_left_match` (LEFT-join null-pad flag). Cleanest loop = process ONE right tuple per iteration, cap via `while (tuple_batch->size() < batch_size)`.
- **`right_pos_` is needed** because one left tuple can match more rights than `batch_size` → must pause mid-inner-scan and resume without duplicating.
- **Advance `left_pos_` ONLY when `right_pos_ >= right_tuples_.size()`** (scan complete), NOT on a batch-full pause. Reset `did_left_match=false` at the SAME point (advance), and KEEP it across a pause (member persists).
- **LEFT join**: after scanning all rights for a left tuple, if `!did_left_match && GetJoinType()==LEFT`, emit `left ++ NULL-pad`. Match = `!v.IsNull() && v.GetAs<bool>()` (predicate NULL is NOT a match).
- **Output tuple** = left cols ++ right cols (see `InferJoinSchema` in plan_node.cpp: left first, then right). Build via helper `ConstructOutTuple(left, right*)`: loop `left_schema.GetColumnCount()` then `right_schema.GetColumnCount()` — use `GetColumnCount()` NOT `Tuple::GetLength()` (that's BYTE length!). For null-pad pass `right=nullptr` and use `ValueFactory::GetNullValueByType(right_schema.GetColumn(i).GetType())`. Bound loop by schema count so `nullptr` is never dereferenced. Emit dummy RID (`RID{}`).
- **p3.10 line 54 requires NestedIndexJoin**: `set force_optimizer_starter_rule=yes` + an index on the inner join key makes the optimizer rewrite the join to NestedIndexJoin. So p3.10 won't fully pass until NIJ is implemented.

### NestedLoopJoin — the `nlj_init_check` gotcha (p3.10 line 309+)
- The grader's `ensure:nlj_init_check` asserts `right->GetInitCount() + 1 >= left->GetNextCount()` (execution_engine.h PerformChecks). It FORCES you to re-`Init()` the right executor once per left `Next()` call (i.e., per left BATCH), not once total.
- **`BUSTUB_BATCH_SIZE = 20`**. So a left table > 20 rows needs multiple `left->Next()` calls; materializing right only once in `Init()` → `right_init=1` → check fails (`1+1 >= 6` false). Tables ≤ 20 rows pass by accident.
- **Fix**: move right materialization OUT of `Init()` and INTO the left-refill block in `Next()`. Each time `left_executor_->Next()` succeeds: `right_executor_->Init(); right_tuples_.clear(); right_rids_.clear(); right_pos_ = 0;` THEN re-drain right. The `Init()` per batch satisfies the check; the `clear()` is CRITICAL (without it `right_tuples_` accumulates one full copy of the right table per batch → duplicate output rows for non-first tuples in batches ≥2). This is functionally block-NLJ (re-scan inner per outer batch) — same results, just conforms to the required re-Init pattern.
- Results-wise materialize-once is also correct, but the check forbids it (a child executor is a single-use stream; re-reading requires re-`Init()`; the grader wants to see that).

### NestedIndexJoin — Done (p3.13 PASS; unblocks p3.10 line 54)
- **Design**: stream the OUTER via `child_executor_` (the single child = outer table); probe the inner table's index on-demand per outer tuple. NO materialization of the inner side. Init only caches handles: `child_executor_->Init()`, `inner_table_info_ = GetCatalog()->GetTable(plan_->GetInnerTableOid())`, `inner_index_info_ = GetCatalog()->GetIndex(plan_->GetIndexOid())`. Both return `std::shared_ptr<TableInfo>`/`shared_ptr<IndexInfo>` (NOT raw pointers).
- **No B+Tree cast needed**: `ScanKey` is virtual on the base `Index` class, so `inner_index_info_->index_->ScanKey(...)` works polymorphically. (IndexScan needed the `BPlusTreeIndexForTwoIntegerColumn` cast only for the ordered-scan iterator, which isn't on the base class. NIJ does pure point lookups.)
- **Build the probe key**: `Value k = plan_->KeyPredicate()->Evaluate(&outer_tuple, child_executor_->GetOutputSchema());` then `Tuple key{{k}, &inner_index_info_->key_schema_};` then `index_->ScanKey(key, &inner_rids_, exec_ctx_->GetTransaction())`. KeyPredicate reads a column FROM the outer tuple → schema+tuple are REQUIRED (unlike IndexScan's constant key where they're ignored). Key tuple built with the INDEX's `key_schema_`.
- **Resume state** (members): outer buffer + `outter_tuple_pos_`; per-outer-tuple `inner_rids_` + `inner_rid_pos_`; `did_outter_tuple_match_`; `should_fetch_inner_rids_` (probe-once flag = NIJ analog of NLJ pause/resume). Probe once per outer tuple; on batch-full pause, keep `should_fetch=false` so you DON'T re-probe on resume.
- **Deleted-tuple skip is MANDATORY here** (unlike NLJ): NIJ fetches inner tuples via `inner_table_info_->table_->GetTuple(rid)` DIRECTLY from the heap, bypassing the pipeline — so it must replicate SeqScan's `is_deleted_` skip itself. Advance `inner_rid_pos_` even for deleted RIDs (else infinite loop).
- **`did_outter_tuple_match_` (not `inner_rids_.empty()`) drives LEFT null-pad**: a non-empty RID list can still yield 0 emits if all matches are deleted, so track emitted-non-deleted-matches. Reset it in the probe block (per outer tuple), keep it across a pause.
- **Multiple RIDs per outer tuple**: duplicate index keys → multiple RIDs → multiple output rows (bag semantics, correct — never dedup). Re-probe on-demand per outer tuple even for repeated keys (each outer row is an independent join).
- **NIJ has no `nlj_init_check`** (single child, no left/right pair) — so the per-batch re-Init dance doesn't apply.

### IntermediateResultPage — Done (header-only, verified 2026-07-12)
- **File**: `src/include/storage/page/intermediate_result_page.h` — **HEADER ONLY, NO .cpp** (none exists, none in CMake; all methods inline; do NOT add a .cpp). Only include reference is `external_merge_sort_executor.h:24`.
- **Purpose**: on-disk page holding intermediate tuples; SHARED by HashJoin (partitions) AND ExternalMergeSort (sorted runs). Design is content-agnostic (a bag of tuples) — stores tuples only, NOT keys, so both features reuse it.
- **Layout = slotted page (grows inward), mirrors `TablePage` minus `TupleMeta`**:
  - Header = `struct Header { page_id_t next_page_id_; uint16_t num_tuples_; }` → **8 bytes** (next_page_id 4 + num_tuples 2 + 2 pad). `next_page_id_` is currently UNUSED (chain tracked externally by `MergeSortRun.pages_`), harmless.
  - Slot = `using TupleInfo = std::pair<uint16_t, uint16_t>` = `{offset, size}` = **4 bytes**. Slot directory (FAM `TupleInfo tuple_info_[0]`) grows forward from byte 8; tuple payloads grow backward from `BUSTUB_PAGE_SIZE` (=**8192**, NOT 4096).
  - `sizeof(IntermediateResultPage) == 8` (static_assert). FAM `tuple_info_[0]` + `char page_begin_offset_[0]` are zero-length arrays (0 bytes; the class is a `reinterpret_cast` VIEW over a BPM frame, owns no bytes).
- **Methods (all inline)**: `Init()` (num_tuples_=0), `GetNumTuples() const`, `GetTuple(idx) const` (bounds-check → `Tuple(RID{}, base+off, size)`), `GetNextTupleOffset(tuple) const` (fullness check → optional offset), `InsertTuple(tuple)` (append; returns bool).
- **Write** = `memcpy(base + off, tuple.GetData(), tuple.GetLength())` (raw payload, NOT `SerializeTo` which adds a 4-byte length prefix → would double-store size). **Read** = public `Tuple(RID{}, base+off, size)` ctor (does resize+memcpy internally; NO friendship with Tuple needed). `base = reinterpret_cast<char*>(this)`.
- **Fixes applied during review**: (1) `GetTuple`/`GetNumTuples` MUST be `const` — read path uses `ReadPageGuard::As<T>()` → `const T*`, won't compile otherwise. (2) removed `index < 0` on unsigned (tautology, fails clang-tidy). (3) unsigned-underflow guard in `GetNextTupleOffset`: `if (tuple.GetLength() > new_tuple_end_offset) return nullopt;` BEFORE subtracting (else huge wrap bypasses the `<` check → garbage offset). (4) added `#include <cstring>`.
- **Key infra facts**: BPM uses `ArcReplacer` (not LRU-K). `FrameHeader::data_` (`vector<char>`) holds the raw bytes on the HEAP; `AsMut<T>()` = `reinterpret_cast<T*>(GetDataMut())` and SETS `is_dirty_=true`; `As<T>()` = const, no dirty. Page classes own NO bytes — `this` IS the frame pointer.

### HashJoin — IN PROGRESS: partitioning/repartitioning implemented, `Next()` still stubbed — RESUME HERE
> **Verified 2026-07-18 by actually building** (`cmake --build . --target sqllogictest`). Design DIVERGED from the earlier recursive-`BuildLeaf` draft — current approach grows the partition vectors iteratively instead of recursing. See bugs below before continuing.

> **Next concrete steps (in order):** (1) fix the 2 compile errors (const-qualify `HashKey::operator==` and `std::hash<HashKey>::operator()`) — confirmed still broken by a real build just now. (2) fix the swapped-args bug in `Init()`'s repartition loop (below) — found this session, not yet fixed. (3) decide whether to fix the orphaned-page leak in `RehashPartiton` (below) — correctness-neutral but ASAN/leak-sanitizer may flag it. (4) implement `Next()` (streams `left_partitions_`, builds `right_tuples_` map per partition, probes, emits). (5) implement `nlj_as_hash_join.cpp` (still a full stub, unchanged). (6) run p3.14/p3.15.
- **Spec** (https://15445.courses.cs.cmu.edu/fall2025/project3/#task3): MUST use **Grace Hash Join** (partition + spill), NOT plain in-memory. Output schema = **all left cols ++ all right cols**. Inner + Left join only. Build side (right) is a pipeline breaker.
- **Current header state** (`src/include/execution/executors/hash_join_executor.h`, verified by Read 2026-07-18):
  - `HashKey{ std::vector<Value> key_values_ }` with `operator==` — logic is correct (NULL never matches; all-equal → true) but **missing trailing `const`** → still a compile error (`unordered_map` needs a const-callable comparator).
  - `std::hash<bustub::HashKey>` specialization in `namespace std` — folds `HashUtil::CombineHashes(HashUtil::HashValue(&v))` skipping NULLs, logic correct, but **`operator()` also missing trailing `const`** → same compile error class.
  - Public constants (now actually in the header, not just planned): `COUNT_LIMIT_FOR_TUPLES_PARTITION = 4096` (renamed from the earlier `MEM_LIMIT_FOR_TUPLES_PARTITION`) and `NUM_PARTITIONS = 8`.
  - Members (renamed from the old draft): `left_partitions_` / `right_partitions_` (`vector<vector<page_id_t>>`, dim0=partition index, dim1=page list — was `left_hash_pages_`/`right_hash_pages_`), `right_partition_tuple_count_` / `left_partition_tuple_count_` (`vector<int>`, running tuple count per partition, used to trigger repartitioning), `right_tuples_` (`unordered_map<HashKey, vector<Tuple>>`, per-partition build map for `Next()`), `curr_left_partition_index_` / `curr_left_tuple_index_` (`Next()` resume cursors — declared but **currently unused**, which triggers `-Werror=unused-private-field`; this resolves itself once `Next()` is written, it's expected noise on an incremental build right now, not a bug to fix separately).
  - Helpers, ALL now implemented in the `.cpp` (this is new since the last note — the whole partitioning pass is written, not just drafted): `InitHashPages`, `MakeHashKey`, `GetHashPartitionIndex` (renamed from the planned `PartitionIndex`), `RehashPartiton` (renamed/typo'd from `RecursivePartitionTuples` — note the missing `i`), `InsertTupleIntoPartition`, `GetIndexesToRepartition`.
- **`intermediate_result_page.h`** — ✅ DONE, unchanged, header-only slotted page (`Init/GetNumTuples/GetTuple/GetNextTupleOffset/InsertTuple`), `sizeof == 8`.
- **`nlj_as_hash_join.cpp`** — unchanged, still `return plan;` full stub.

#### How `Init()` actually works now (iterative growth, not recursion)
1. `InitHashPages(child, key_exprs, partitions, tuple_count)` for each side: creates `NUM_PARTITIONS` (8) fresh pages up front (one guard at a time, not held open across the whole drain — reacquires `WritePage` per tuple via `InsertTupleIntoPartition`, which is less efficient than the originally-planned "hold N guards" design but simpler/correct), drains the child fully via `BUSTUB_BATCH_SIZE` batches, routes each tuple to `partitions[GetHashPartitionIndex(key, salt=0)]`.
2. Loop: `GetIndexesToRepartition(right_partition_tuple_count_)` returns indices where count > `COUNT_LIMIT_FOR_TUPLES_PARTITION`. If empty, done.
3. Otherwise: **append** `NUM_PARTITIONS` new empty partition slots to the END of both `left_partitions_` and `right_partitions_` (vectors grow every round, old indices are never reused for a different layer). Then for each oversized index `i`, call `RehashPartiton` on both sides with the current `repartition_salt`, which re-reads all pages at `partitions[i]`, recomputes `GetHashPartitionIndex(key, salt)`, and — since `salt>0` — offsets the result by `+ NUM_PARTITIONS * salt` to land in this round's newly-appended layer. The old slot `partitions[i]` is cleared to empty (see leak note below) so `Next()` must be prepared to see empty partition slots interleaved with populated ones.
4. `repartition_salt` increments each round. No `MAX_DEPTH` cap exists yet — identical-key skew (all dupes of one join key) would loop forever since no salt splits identical keys. Low risk for p3.14/p3.15's data size, worth a comment/guard eventually but not blocking.

#### ⚠️ Real bug found this session (not yet fixed) — swapped args in `Init()`'s repartition loop
In `hash_join_executor.cpp` `Init()`, the two `RehashPartiton` calls per oversized index `i`:
```cpp
RehashPartiton(right_partitions_, i, repartition_salt, right_child_->GetOutputSchema(),
               plan_->LeftJoinKeyExpressions(), left_partition_tuple_count_);   // should be Right*, right_partition_tuple_count_
RehashPartiton(left_partitions_, i, repartition_salt, left_child_->GetOutputSchema(),
               plan_->RightJoinKeyExpressions(), left_partition_tuple_count_);  // should be Left*
```
The right-side rehash uses **`LeftJoinKeyExpressions()`** against right tuples (should be `RightJoinKeyExpressions()`), and the left-side rehash uses **`RightJoinKeyExpressions()`** against left tuples (should be `LeftJoinKeyExpressions()`) — the two key-expression args are swapped. Both calls also pass `left_partition_tuple_count_` as the counter to update — the right-side call should pass `right_partition_tuple_count_`, otherwise right's counts never get updated post-rehash and left's counter gets corrupted with right's tuple counts. Net effect: repartitioning computes bucket indices from the WRONG table's join columns, which can scatter equal keys into different partitions and break the "equal keys co-locate" invariant `Next()` depends on. **Only matters once repartitioning actually triggers** (>4096 tuples in one partition) — likely doesn't affect p3.14/p3.15 test data, but must fix before trusting HashJoin on larger inputs.

#### Orphaned page leak in `RehashPartiton` (correctness-neutral, cleanliness issue)
`RehashPartiton` does `auto pids = std::move(partitions[index]); partitions[index] = {};` to detach the old page list, then re-inserts tuples elsewhere — but never calls `bpm->DeletePage(pid)` on the old `pids`. Those pages stay allocated in the buffer pool/disk forever. Won't break correctness (the executor never looks at them again) but wastes pages and may trip a leak check under ASAN. Fix = `DeletePage` each old pid after finishing the re-read loop.

#### `Next()` — still `UNIMPLEMENTED`, this is the main remaining piece
Design intent (unchanged from earlier planning, still valid): per partition index `x` (iterate `curr_left_partition_index_` over `0..left_partitions_.size()`, skip empty slots from repartitioning), build `right_tuples_` map from all pages in `right_partitions_[x]` (skip NULL-key right tuples — NULL never matches), then stream `left_partitions_[x]` tuple-by-tuple (`curr_left_tuple_index_`), probe the map, emit `left ++ right` per match, or `left ++ NULLs` once per left tuple if no match and join is LEFT. Needs a match-cursor (like NLJ's `right_pos_`) for when one left tuple's matches don't fit in one batch. Emit dummy `RID{}`.

#### The two-hash mental model (still the crux — internalize this)
- **Partition hash** (`GetHashPartitionIndex`, salted by `repartition_salt`) is COARSE — job is to make equal keys co-locate in the same partition index, not to distinguish all keys perfectly. **Map hash** (`std::hash<HashKey>` + `operator==`, used inside `right_tuples_`) is EXACT — job is to distinguish keys within one already-loaded partition. Don't conflate them; `Next()` should do a plain `map[k]` lookup, never re-apply `GetHashPartitionIndex`.
- **Co-location invariant**: a partition is a mixed bag of many distinct keys that collided under `% NUM_PARTITIONS` — the only guarantee is `K_L == K_R ⇒ part(K_L) == part(K_R)`, so a left tuple's matches are always in the same-index right partition. This is why the swapped-args bug above matters: if left and right computed their partition index from *different* columns, this invariant breaks silently.

- **Tests**: p3.14-hash-join.slt, p3.15-multi-way-hash-join.slt. Run: `build/bin/bustub-sqllogictest <test.slt> --verbose -d --in-memory` (build target `make sqllogictest`; verified working build command: `cd build && cmake --build . --target sqllogictest -j$(sysctl -n hw.ncpu)`).
- **Verify tool caveat**: `WebFetch` fails on the course site with a TLS cert error; use `curl -sL <url>` instead.

### P3 Patterns Learned
- **Modification executors (Insert/Delete/Update)** return a single integer tuple with the row count, not the actual tuples
- **`has_returned_` flag**: needed on all modification executors to prevent infinite `Next()` loop; reset in `Init()`
- **Index updates**: after insert/delete, iterate `GetTableIndexes()`, build key via `KeyFromTuple(table_schema, key_schema, GetKeyAttrs())`, call `InsertEntry`/`DeleteEntry`
- **Cache `GetTableIndexes()`** outside the loop to avoid repeated catalog lookups
- **Declare child batch vectors outside the loop** to avoid re-allocation each iteration
- **Child init**: parent `Init()` must call `child_executor_->Init()`
- **`std::optional<TableIterator>`**: use for members with no default constructor, init via `emplace()` in `Init()`
- **Update = delete + insert**: mark old tuple deleted, evaluate `target_expressions_` to build new tuple, insert new tuple
- **Filter predicate**: only SeqScan needs to check it; Insert/Update/Delete receive pre-filtered tuples from child
- **`Next()` empty-batch guard**: when manually skipping deleted tuples, wrap in outer loop so `Next()` never returns `true` with an empty batch — keep iterating until batch has tuples or source is exhausted, then `return !tuple_batch->empty()`
- **`pred_keys_` is OR (union)**: each entry is a separate point lookup key; AND predicates are not converted to IndexScan by the optimizer

---

## Task #2 — Code Recon (verified from source, ready to implement)

### Aggregation (`aggregation_executor.{h,cpp}` + `plans/aggregation_plan.h`)
- **Header stubs to uncomment**: `SimpleAggregationHashTable aht_;` and `SimpleAggregationHashTable::Iterator aht_iterator_;` (both `// TODO(Student)` commented out).
- **Already-provided helpers** (do NOT rewrite): `MakeAggregateKey(tuple)`, `MakeAggregateValue(tuple)`, `InsertCombine(key,val)`, `GenerateInitialAggregateValue()`, `Begin()/End()`, `Iterator` with `Key()`/`Val()`/`++`/`==`/`!=`.
- **`GenerateInitialAggregateValue()` (already correct)**: `CountStarAggregate` → `ValueFactory::GetIntegerValue(0)`; all others → `ValueFactory::GetNullValueByType(TypeId::INTEGER)` (NULL). This is exactly the empty-table behavior.
- **`CombineAggregateValues(AggregateValue *result, const AggregateValue &input)` — the ONE method to implement**. Switch over `agg_types_[i]`:
  - `CountStarAggregate`: `result += 1` always (use `.Add(GetIntegerValue(1))`).
  - `CountAggregate`: if input not null → if result null set to 0 first, then +1 (count of non-null).
  - `SumAggregate`: if input not null → result null? set=input : result=result.Add(input).
  - `MinAggregate`: if input not null → result null? set=input : result=result.Min(input).
  - `MaxAggregate`: if input not null → result null? set=input : result=result.Max(input).
- **`AggregationType` enum**: `CountStarAggregate, CountAggregate, SumAggregate, MinAggregate, MaxAggregate`.
- **Plan accessors**: `GetGroupBys()`, `GetAggregates()`, `GetAggregateTypes()`, `OutputSchema()`, `GetChildPlan()`.
- **Init()**: init child, drain child fully, `aht_.InsertCombine(MakeAggregateKey, MakeAggregateValue)` per tuple; set `aht_iterator_ = aht_.Begin()`. **Empty-table + no group-by edge case**: if table empty AND `GetGroupBys().empty()`, must still emit ONE row = initial aggregate value (CountStar=0, rest NULL). Handle with a flag.
- **Next()**: iterate `aht_iterator_` != `aht_.End()`, output tuple = group_bys_ ++ aggregates_ concatenated (match OutputSchema column order: group-bys first, then aggregates), advance iterator, fill batch.
- **`Value` ops**: `.Add`, `.Min`, `.Max`, `.CompareLessThan`→`CmpBool`, `.IsNull()`. `CmpBool` enum: `CmpFalse/CmpTrue/CmpNull` in `type/type.h`.

### NestedLoopJoin (`nested_loop_join_executor.{h,cpp}` + `plans/nested_loop_join_plan.h`)
- **Header**: must ADD members for `left_executor_` / `right_executor_` (constructor gets them but stub doesn't store). Plus buffering state.
- **Constructor** already validates join type is INNER or LEFT.
- **Plan accessors**: `Predicate()`, `GetJoinType()`, `GetLeftPlan()`, `GetRightPlan()`. Left/right schemas via `Get{Left,Right}Plan()->OutputSchema()`.
- **`JoinType`** (in `binder/table_ref/bound_join_ref.h`): `INVALID=0, LEFT=1, RIGHT=3, INNER=4, OUTER=5`. Only LEFT + INNER needed.
- **Predicate eval**: `plan_->Predicate()->EvaluateJoin(&left, left_schema, &right, right_schema) -> Value`. Match = `!v.IsNull() && v.GetAs<bool>()`.
- **Output tuple**: concat left cols then right cols into `vector<Value>`, `Tuple(values, &GetOutputSchema())`. Left-join no-match → right cols = `ValueFactory::GetNullValueByType(right_schema.GetColumn(i).GetType())`.
- **Design note**: children are batch executors. Simplest correct approach — in `Init()`, fully materialize the RIGHT side into a `std::vector<Tuple>` (right is the inner/rescanned side); stream LEFT via child batches. Track a `left_matched_` bool per left tuple for LEFT join NULL-padding. Careful with batch boundaries (buffer emitted rows in a queue, drain into batch).

### NestedIndexJoin (`nested_index_join_executor.{h,cpp}` + `plans/nested_index_join_plan.h`)
- **Single child** = outer table (streamed). Inner table is probed via index.
- **Plan accessors**: `KeyPredicate()`, `GetJoinType()`, `GetInnerTableOid()`, `GetIndexOid()`, `GetIndexName()`, `InnerTableSchema()`, `GetChildPlan()`, `OutputSchema()`.
- **Init()**: `index_info_ = GetCatalog()->GetIndex(GetIndexOid())`; `table_info_ = GetCatalog()->GetTable(GetInnerTableOid())`. `IndexInfo` has `index_` (unique_ptr<Index>), `key_schema_`. `TableInfo` has `table_` (TableHeap), `schema_`.
- **Probe**: `Value k = KeyPredicate()->Evaluate(&outer_tuple, outer_schema)`; `Tuple key{{k}, &index_info_->key_schema_}`; `index_info_->index_->ScanKey(key, &rids, txn)`.
- **Fetch inner**: `auto [meta, tuple] = table_info_->table_->GetTuple(rid); if (!meta.is_deleted_) ...`.
- **Join**: INNER → emit outer++inner per matching RID; LEFT → if no (non-deleted) match, emit outer ++ NULL-padded inner cols (`InnerTableSchema()` column types).

### Shared batch `Next()` pattern (from seq_scan_executor.cpp:43)
```cpp
tuple_batch->clear(); rid_batch->clear();
tuple_batch->reserve(batch_size); rid_batch->reserve(batch_size);
while (<source not exhausted> && batch_size > 0) { ... push; batch_size--; }
return !tuple_batch->empty();
```
- Tuple ctor: `Tuple(std::vector<Value> values, const Schema *schema)`.
- Modification executors use `Value(TypeId::INTEGER, count)`; here we mostly use ValueFactory + concatenated Values.
- Pipeline breakers (Aggregation) build in `Init()`, emit in `Next()`. Joins can stream.

**Suggested order**: Aggregation → NestedLoopJoin → NestedIndexJoin. Tests: `p3.07-*`..`p3.13-*`.

---

## TODO: Fix p3.05 — Nested OR in SeqScan→IndexScan Optimizer (FIXED — commit 026f069)

**Test**: `p3.05-index-scan-btree.slt` line 124: `select * from t1 where v2 = 10 or v2 = 20 or v2 = 30 or v2 = 40`
**Error**: "IndexScan not found" — optimizer doesn't convert this to IndexScan

**Root cause**: The current optimizer only handles flat 2-way OR (`col=x OR col=y`). The parser builds nested OR trees for 3+ conditions:
```
        OR
       /  \
      OR   (v2=40)
     /  \
    OR   (v2=30)
   /  \
(v2=10) (v2=20)
```

**Fix needed**: Add a recursive `FlattenOrExpr` helper that walks nested OR trees and collects all `col=const` equalities into a single `pred_keys` vector. Must verify all leaves reference the same column. If any leaf is not a valid equality or references a different column, bail out (keep as SeqScan).

**File**: `src/optimizer/seqscan_as_indexscan.cpp`

---

## Known Issues

### ASAN Hangs on macOS ARM64
ASAN deadlocks during init (before `main()`). Disable sanitizers to work around:
```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug -DBUSTUB_SANITIZER=
```
Re-enable: `cmake .. -DCMAKE_BUILD_TYPE=Debug` (omit the flag).

---
---

# Project 2: B+ Tree Index (COMPLETED)

## P2 Tasks Overview

| Task | Description | Status |
|------|-------------|--------|
| Task #1 | B+Tree Pages (Base, Internal, Leaf) | ✅ DONE |
| Task #2 | B+Tree Operations (Insert, Delete, Search) | ✅ DONE |
| Task #3 | Index Iterator | ✅ DONE |
| Task #4 | Concurrency Control (latch crabbing) | ✅ DONE |

## P2 File Locations

| Component | Header | Source |
|-----------|--------|--------|
| Base Page | `src/include/storage/page/b_plus_tree_page.h` | `src/storage/page/b_plus_tree_page.cpp` |
| Internal Page | `src/include/storage/page/b_plus_tree_internal_page.h` | `src/storage/page/b_plus_tree_internal_page.cpp` |
| Leaf Page | `src/include/storage/page/b_plus_tree_leaf_page.h` | `src/storage/page/b_plus_tree_leaf_page.cpp` |
| B+ Tree | `src/include/storage/index/b_plus_tree.h` | `src/storage/index/b_plus_tree.cpp` |
| Iterator | `src/include/storage/index/index_iterator.h` | `src/storage/index/index_iterator.cpp` |
| Page Guards | `src/include/storage/page/page_guard.h` | `src/storage/page/page_guard.cpp` |
| Test Utils | `test/include/storage/b_plus_tree_utils.h` | — |
| Tests | — | `test/storage/b_plus_tree_*.cpp` |

---

## P2 Key Design Rules

### Tombstone Underflow: Use PHYSICAL Size (Not Logical)
Tombstones defer physical deletion to keep pages above min_size. All underflow/safe checks must use `GetSize()` (physical), not `GetSize() - GetTombstonesSize()` (logical).

- **Remove() underflow**: `GetSize() >= GetMinSize()` → page is fine
- **TraverseNodesToLeaf safe check (delete)**: `GetSize() > GetMinSize()` → safe to release ancestors
- **RemoveOptimistic safe check**: `GetSize() <= GetMinSize()` → fall back to pessimistic
- Physical size only drops when tombstone buffer overflows and `DeleteOldestKeyInTombstones` is called

### LeafPage KeyAt/ValueAt Return By Reference
`KeyAt()` and `ValueAt()` return `const KeyType &` / `const ValueType &` (not by value). Required because `operator*()` returns `std::pair<const KeyType &, const ValueType &>`.

### InsertKVToLeafPage Must Update Tombstone Indices
When inserting a new key, `ShiftKeyAndValueRight(pos)` shifts entries at positions >= pos right by 1. Tombstone indices >= pos must also be incremented, otherwise they point to the wrong entries. Call `IncrementTombstonesIndexesFrom(index_pos)` before the shift.

### GetMinSize — `ceil(max_size / 2)` Everywhere
Single formula in `BPlusTreePage::GetMinSize()`. No override on internal pages.

### LeafPage::Init() Must Pass `leaf_max_size_`
`leaf_page->Init(leaf_max_size_)` — NOT default `Init()`. Without this, max_size defaults to `LEAF_PAGE_SLOT_CNT` (~253).

### NumTombs=0 → Physical Deletion Only
Use `if constexpr (LEAF_PAGE_TOMB_CNT == 0)` to branch. `IsTombstonesFull()` returns true when buffer is zero-length.

---

---

## Architecture Overview

### Class Hierarchy
```
BPlusTreePage (base)
├── BPlusTreeHeaderPage        — root_page_id storage
├── BPlusTreeInternalPage      — keys + child page_id pointers
└── BPlusTreeLeafPage          — key-value pairs + tombstone buffer + sibling pointer
```

### Context Object
`Context` encapsulates all page guards for a single tree operation:
- `header_page_` — WritePageGuard for the header page
- `root_page_id_` — cached root page ID
- `write_set_` — deque of WritePageGuards (ancestors at front, current at back)
- `read_set_` — deque of ReadPageGuards (used in optimistic path)
- Guards auto-release via RAII when Context is destroyed

### Page Memory Layout

**Internal Page** (12-byte header):
```
[PageType(4)][Size(4)][MaxSize(4)][KEY_0(invalid)][KEY_1]...[KEY_n][PAGEID_0]...[PAGEID_n]
```
- First key (index 0) is always INVALID; valid keys start at index 1

**Leaf Page** (16-byte header + tombstone metadata):
```
[PageType(4)][Size(4)][MaxSize(4)][NextPageId(4)][num_tombstones_(8)]
[tombstones_[0..k]][KEY_0]...[KEY_n][RID_0]...[RID_n]
```

### Public API
| Method | Returns | Purpose |
|--------|---------|---------|
| `Insert(key, value)` | `bool` | Insert K-V pair; false on duplicate |
| `Remove(key)` | `void` | Delete key; handles underflow/rebalancing |
| `GetValue(key, result*)` | `bool` | Point query; populates result vector |
| `Begin()` | `Iterator` | Iterator to first leaf entry |
| `Begin(key)` | `Iterator` | Iterator starting at key |
| `End()` | `Iterator` | End sentinel |

### Key Private Methods
| Method | Purpose |
|--------|---------|
| `TraverseNodesToLeaf` | Navigate to leaf with configurable guard management |
| `OptimisticTraverseNode` | Read-lock internals, write-lock leaf only |
| `InsertOptimistic` | Fast-path insert; returns nullopt if splits needed |
| `RemoveOptimistic` | Fast-path delete; returns false if rebalancing needed |
| `SplitLeafPage` / `SplitInternalPage` | Split full pages, return (promoted_key, new_guard, new_page_id) |
| `InsertToParent` | Recursive upward key propagation after split |
| `RemoveKeyValueInInternalPage` | Recursive removal with rebalancing |
| `RedistributeLeafPage{Left,Right}Sibling` | Borrow entries from sibling |
| `MergeTwoLeafPages` / `MergeTwoInternalPages` | Merge underfull pages |

### Latch Crabbing Protocol
- **Pessimistic**: Write-latch from root down; release ancestors when child is "safe"
- **Optimistic**: Read-latch internals, write-latch only leaf; fall back to pessimistic if unsafe
- **Safe page**: has room for insert OR above min_size for delete
- `DrainQueueUntilSize(queue, size)` releases guards from front of deque

### Page Guard Semantics
| | ReadPageGuard | WritePageGuard |
|--|---------------|----------------|
| Lock | Shared (multiple readers) | Exclusive (single writer) |
| Access | `As<T>()` (read-only) | `AsMut<T>()` (mutable) |
| Drop() | Unpin only (no flush) | Unpin only (no flush) |
| Move | Transfers ownership, invalidates source | Same |

---

## Tombstone Spec

- Tombstones MUST be maintained across ALL operations (split/merge/redistribute)
- When buffer full: evict OLDEST tombstone (FIFO)
- During coalesce: src's tombstones are considered NEWER than dest's
- `MergeTwoLeafPages`: copies all entries, rebuilds tombstones (dest first = older, src second = newer), evicts if physical > max_size

### Tombstone Methods on LeafPage
| Method | Purpose |
|--------|---------|
| `AddIndexToTombstones(index)` | Mark entry as tombstoned |
| `RemoveIndexFromTombstones(index)` | Unmark entry |
| `DeleteOldestKeyInTombstones()` | Evict oldest (FIFO), shift left, adjust remaining indices |
| `IsIndexInTombstones(index)` | O(num_tombstones) linear scan |
| `IsTombstonesFull()` | True when buffer capacity reached |
| `IncrementTombstonesIndexesFrom(pos)` | Adjust indices after insert shift |
| `DecreaseAllTombstonesIndexes()` | Adjust indices after physical delete |
| `ClearTombstones()` | Reset buffer |

### Template Instantiations
```cpp
BPlusTree<GenericKey<4>, RID, GenericComparator<4>>       // Key size 4
BPlusTree<GenericKey<8>, RID, GenericComparator<8>>       // Key size 8, NumTombs=0 (default)
BPlusTree<GenericKey<8>, RID, GenericComparator<8>, 3>    // NumTombs=3
BPlusTree<GenericKey<8>, RID, GenericComparator<8>, 2>    // NumTombs=2
BPlusTree<GenericKey<8>, RID, GenericComparator<8>, 1>    // NumTombs=1
BPlusTree<GenericKey<8>, RID, GenericComparator<8>, -1>   // NumTombs=-1 → TOMB_CNT=0
BPlusTree<GenericKey<16>, RID, GenericComparator<16>>     // Key size 16
BPlusTree<GenericKey<32>, RID, GenericComparator<32>>     // Key size 32
BPlusTree<GenericKey<64>, RID, GenericComparator<64>>     // Key size 64
```

---

## Remove() Flow
```
1. Tree empty → return
2. Find leaf, find key index (binary search)
3. Key not found / already tombstoned → return
4. Perform deletion:
   - NumTombs=0: physical delete (ShiftLeft + SetSize)
   - Buffer not full: AddIndexToTombstones
   - Buffer full: DeleteOldestKeyInTombstones, re-find key, AddIndexToTombstones
5. Check PHYSICAL size (GetSize()):
   - If >= min_size → return
   - If root → allow underfull, set empty if all tombstoned
   - If < min_size → redistribute or merge, cascade up
```

---

## Task #3: Index Iterator — DONE

### Key Implementation Details
- Constructor takes `ReadPageGuard leaf_guard` (moved from caller) — avoids double read-latch deadlock
- `LoadPageAndIterator()`: iterative `while(true)` loop (not recursive) following sibling chain
- Tombstone skipping via `unordered_set<size_t>` of tombstoned indices — O(1) membership checks
- `FindAndSetValidIndex()` skips consecutive tombstoned entries; triggers page transition if all entries tombstoned
- `operator==` compares `page_id_` AND `key_index_`; `IsEnd()` checks `page_id_ == INVALID_PAGE_ID`
- `key_index_` must be reset to 0 when reaching end (consistency between `operator==` and `IsEnd()`)
- Iterator holds `ReadPageGuard` for entire lifetime — blocks exclusive writers on that page
- Tombstone set is cached per page (snapshot at load time, not real-time)

---

## Task #4: Concurrency Control — DONE

### Optimistic Latch Crabbing
- Read-latch crabbing through internals, write-latch only the leaf
- If leaf operation is "safe" → done; if unsafe → fall back to pessimistic
- Parent read latch held during read→write gap on leaf (prevents structural changes)

### Test Status
| Test | Status |
|------|--------|
| OptimisticInsertTest | ✅ PASS |
| OptimisticDeleteTest | ✅ PASS |
| InsertTest1/2, DeleteTest1/2 (concurrent) | ✅ PASS |
| MixTest1SingleThread | ✅ PASS |
| MixTest1/MixTest2 (concurrent) | ✅ PASS |

---

## Bugs Fixed (All Sessions)

| Bug | Fix | Key Detail |
|-----|-----|------------|
| **Tombstone underflow used logical size** | Changed to physical `GetSize()` in Remove, TraverseNodesToLeaf, RemoveOptimistic | Tombstones exist to prevent merges; logical check defeated this |
| **operator*() stack-use-after-return** | `KeyAt()`/`ValueAt()` return `const&` instead of by value | Pair of references was binding to dead temporaries |
| **InsertKVToLeafPage tombstone index corruption** | `IncrementTombstonesIndexesFrom(index_pos)` before ShiftRight | ShiftRight moved entries but didn't update tombstone indices |
| Buffer pool exhaustion on insert/delete | `.Drop()` guards before recursive `InsertToParent`/`RemoveKeyValueInInternalPage` | Prevents pin accumulation on call stack |
| Iterator deadlock (double read-latch) | Constructor takes `ReadPageGuard` moved from caller | Same page can't be read-latched twice |
| Iterator stack overflow | `LoadPageAndIterator` uses iterative loop | Long chains of tombstoned pages |
| Header write lock released early | Access header through `ctx.header_page_` without moving guard | Guard stays alive until `ctx` destructor |
| BPM Drop() data race | Removed flush-after-unlock and redundant `is_write_` writes | Drop() should only unpin, not flush |

---

## C++ Patterns

```cpp
// Template keyword for dependent types
auto *page = guard.template As<LeafPage>();    // With auto guard
auto *page = guard.As<LeafPage>();             // With explicit ReadPageGuard

// NumTombs=0 branching
if constexpr (LEAF_PAGE_TOMB_CNT == 0) { /* physical */ } else { /* tombstone */ }
```

C++ Standard: **C++17** (no C++20 features)

---

## Testing

### Test Files
| Test File | Coverage |
|-----------|----------|
| `b_plus_tree_insert_test` | BasicInsertTest, OptimisticInsertTest, InsertTest1NoIterator, InsertTest2 |
| `b_plus_tree_delete_test` | DeleteTestNoIterator, OptimisticDeleteTest, SequentialEdgeMixTest |
| `b_plus_tree_concurrent_test` | InsertTest1/2, DeleteTest1/2, MixTest1/2, MixTest1SingleThread (multi-threaded) |
| `b_plus_tree_tombstone_test` | 4 tests — ALL DISABLED by default |
| `b_plus_tree_sequential_scale_test` | BasicScaleTest (5000 keys) |

### Build & Run Commands
```bash
cd build
make b_plus_tree_insert_test b_plus_tree_delete_test -j$(sysctl -n hw.ncpu)
make b_plus_tree_tombstone_test -j$(sysctl -n hw.ncpu)
make b_plus_tree_sequential_scale_test -j$(sysctl -n hw.ncpu)
make b_plus_tree_concurrent_test -j$(sysctl -n hw.ncpu)
# Tombstone tests are DISABLED by default:
./test/b_plus_tree_tombstone_test --gtest_also_run_disabled_tests
```

### Test Setup Pattern
```cpp
auto key_schema = ParseCreateStatement("a bigint");
GenericComparator<8> comparator(key_schema.get());
DiskManagerUnlimitedMemory disk_manager;
BufferPoolManager bpm(50, &disk_manager);
page_id_t header_page_id = bpm.NewPage();
BPlusTree<GenericKey<8>, RID, GenericComparator<8>> tree(
    "foo_pk", header_page_id, &bpm, comparator, leaf_max_size, internal_max_size);
```

### Formatting (required for grade)
```bash
make format && make check-lint && make check-clang-tidy-p2
```

---

## P2 Remaining TODOs

1. Run formatting/linting before submission
