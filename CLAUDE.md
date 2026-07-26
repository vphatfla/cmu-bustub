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
| Task #3 | Hash Join & Optimization (IntermediateResultPage ✅, HashJoin ✅, NLJ→HashJoin optimizer ✅) | ✅ DONE — p3.14 & p3.15 both pass |
| Task #4 | Sort, Limit, TopN & Window Functions (ExternalMergeSort ✅, Limit ✅, TupleComparator+GenerateSortKey ✅, TopN, Sort+Limit→TopN, WindowFunction) | 🔶 IN PROGRESS — Limit, TupleComparator, ExternalMergeSort all done (p3.16-sort-limit.slt PASSES); TopN next, see "ExternalMergeSort" section |

### Verified State (2026-07-21 — read from source, not cache)
- **Tasks #1 & #2**: fully implemented & verified (9 executors + SeqScan→IndexScan optimizer). Only cosmetic comment typos (`outter`, `experission`, `comparsion`, `bnreak`).
- **Task #3**: ✅ DONE as of 2026-07-20 — `intermediate_result_page.h`, `hash_join_executor.{h,cpp}` (Init/Next fully implemented), and `nlj_as_hash_join.cpp` (optimizer rule implemented, recursive AND-flattening) are all complete. `p3.14-hash-join.slt` and `p3.15-multi-way-hash-join.slt` both pass end-to-end with zero failures. See the full "HashJoin" section below for implementation details and the bug history.
- **Task #4**: re-verified 2026-07-25 (fresh agent read of every file, not cache). **`limit_executor.{h,cpp}` is fully implemented** (uncommitted) — ctor stores `plan_`/`child_executor_`; `Init()` re-inits child, clears `child_tuples_`/`child_rids_` buffers, resets `limit_ = plan_->GetLimit()`; `Next()` refills the buffer from the child in `BUSTUB_BATCH_SIZE` chunks and copies into the output batch while `batch_size>0 && limit_>0`, decrementing both. **`execution_common.cpp` (`TupleComparator::operator()` + `GenerateSortKey`) is also fully implemented and verified correct** (uncommitted) — see the dedicated "TupleComparator + GenerateSortKey — DONE" subsection under "Task #4 — Code Recon" for the full null-handling bug history (went through 4 iterations before landing on a correct, antisymmetric implementation). Build confirmed green (`make sqllogictest`); running `p3.16-sort-limit.slt` crashes immediately on `ExternalMergeSortExecutor`'s `UNIMPLEMENTED` stub (expected — that executor is next, not a regression). Everything else still stubbed: `external_merge_sort_executor.{h,cpp}` (Iterator + ctor/Init/Next `UNIMPLEMENTED`, only `template class ...<2>` instantiated), `topn_executor.cpp` (empty ctor — doesn't even store `plan_`/`child_executor_` despite both being declared as members, `NotImplementedException`, +`GetNumInHeap` throws), `topn_per_group_executor.cpp` (ctor stores plan+child, `NotImplementedException`), `window_function_executor.cpp` (ctor done, Init throws, Next→false), `sort_limit_as_topn.cpp` (returns plan unchanged, no recursion into children). Full per-file breakdown with exact stub code and design notes in "Task #4 — Code Recon" section below.
- **`ExternalMergeSortExecutor` — ✅ DONE, verified 2026-07-26**: `p3.16-sort-limit.slt` passes completely (exit 0, zero `wrong result` occurrences), including the 100,000-row `__mock_external_merge_sort_input` table, multi-column `ORDER BY` with mixed `ASC`/`DESC`, all `NULLS FIRST`/`NULLS LAST`/default combinations, and nested subqueries with `LIMIT`. Full implementation details and bug history (10 distinct bugs found and fixed via pair-programming review) in the dedicated "ExternalMergeSort" section below. **Next milestone: `TopNExecutor` + `sort_limit_as_topn` optimizer rule.**
- **Stray debug files at repo root**: `expected.log`/`result.log` (untracked) are sqllogictest's auto-dump-on-mismatch output (`tools/sqllogictest/sqllogictest.cpp:ResultCompare`) from an ad-hoc manual run — `expected.log` has 8 rows (`"0 10"`..`"7 17"`), `result.log` has all 10 rows of `test_simple_seq_2`. No `LIMIT 8` query exists in any tracked `.slt`, so this was likely a manual/interactive query, not a real test failure — and it predates confirming Limit's logic is correct. Safe to delete once confirmed stale; not investigated further since Limit itself checks out.

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
| **TupleComparator** | ✅ DONE — `operator()` + `GenerateSortKey()` implemented in `execution_common.cpp`; ASC/DESC + NULL placement (NULLS_FIRST/LAST/DEFAULT) verified correct |
| **ExternalMergeSort** | 2-way external merge sort; spill to `IntermediateResultPage`; delete temp pages after; `std::sort` only on single-page data; pipeline breaker |
| **Limit** | Cap output to `plan_->GetLimit()` tuples; trivial |
| **WindowFunction** | PARTITION BY + ORDER BY; frame: with ORDER BY = first-to-current, without = entire partition; reuse aggregation logic; implement RANK (with ties); types: CountStar, Count, Sum, Min, Max, Rank |

---

## P3 Implementation Order (Suggested)

```
Phase 1 (Task #1):  SeqScan → Insert → Delete → Update → IndexScan → SeqScan→IndexScan optimizer
Phase 2 (Task #2):  Aggregation → NestedLoopJoin → NestedIndexJoin
Phase 3 (Task #3):  IntermediateResultPage → HashJoin → NLJ→HashJoin optimizer
Phase 4 (Task #4):  Limit ✅ → TupleComparator ✅ → ExternalMergeSort ✅ → TopN (next) → WindowFunction
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
- **Methods (all inline)**: `Init()` (num_tuples_=0), `GetNumTuples() const`, `GetTupleAtIndex(idx) const` (bounds-check → `Tuple(RID{}, base+off, size)`; **note: named `GetTupleAtIndex`, not `GetTuple` — corrected 2026-07-25**), `GetNextTupleOffset(tuple) const` (fullness check → optional offset), `InsertTuple(tuple)` (append; returns bool).
- **Write** = `memcpy(base + off, tuple.GetData(), tuple.GetLength())` (raw payload, NOT `SerializeTo` which adds a 4-byte length prefix → would double-store size). **Read** = public `Tuple(RID{}, base+off, size)` ctor (does resize+memcpy internally; NO friendship with Tuple needed). `base = reinterpret_cast<char*>(this)`.
- **Fixes applied during review**: (1) `GetTuple`/`GetNumTuples` MUST be `const` — read path uses `ReadPageGuard::As<T>()` → `const T*`, won't compile otherwise. (2) removed `index < 0` on unsigned (tautology, fails clang-tidy). (3) unsigned-underflow guard in `GetNextTupleOffset`: `if (tuple.GetLength() > new_tuple_end_offset) return nullopt;` BEFORE subtracting (else huge wrap bypasses the `<` check → garbage offset). (4) added `#include <cstring>`.
- **Key infra facts**: BPM uses `ArcReplacer` (not LRU-K). `FrameHeader::data_` (`vector<char>`) holds the raw bytes on the HEAP; `AsMut<T>()` = `reinterpret_cast<T*>(GetDataMut())` and SETS `is_dirty_=true`; `As<T>()` = const, no dirty. Page classes own NO bytes — `this` IS the frame pointer.

### HashJoin — ✅ DONE. p3.14-hash-join.slt AND p3.15-multi-way-hash-join.slt both pass end-to-end, zero failures
> **Verified 2026-07-19/20, end of session, by actually running both test files to completion** (not just building/hand-tracing): `./bin/bustub-sqllogictest ../test/sql/p3.14-hash-join.slt --verbose -d --in-memory` → exit 0, 0 `wrong result` occurrences across 893 lines of output, including the 10,000-row `big_l` cross-join case (`count(*) = 10000`, correct) and all composite-key (2/3/4-way AND) cases. `p3.15-multi-way-hash-join.slt` → exit 0, 0 failures, including chained/multi-way joins (`ensure:hash_join*2`). Task #3 is functionally complete. Remaining P3 work is Task #4 (Sort/Limit/TopN/Window) — see that section.

#### `nlj_as_hash_join.cpp` — DONE this session (previous blocker resolved)
- Two free functions in `namespace bustub` (not class methods, mirrors `seqscan_as_indexscan.cpp`'s `SplitComparisonExpr`/`ResolveExpr` pattern):
  - `SplitEquiJoinExprIntoHashKeys(expr, left_keys*, right_keys*) -> bool` — leaf parser. `dynamic_cast` to `ComparisonExpression`, checks `ComparisonType::Equal`, `dynamic_cast`s both `GetChildAt(0)`/`GetChildAt(1)` to `ColumnValueExpression`, routes by `GetTupleIdx()` (handles BOTH orderings — `tidx0==0,tidx1==1` and `tidx0==1,tidx1==0` — since the binder does NOT normalize operand order; confirmed by `p3.14:543/549`'s "switch left and right table column expressions" test). Bails (`false`) on anything else (constant operand, same-side operands, non-equality).
  - `ResolveNLJToHashKey(expr, left_keys*, right_keys*) -> bool` — recursive flattener. If `expr` isn't a `LogicExpression`, dispatch straight to the leaf parser. If it IS `LogicType::And`, recurse into **both children via itself** (`ResolveNLJToHashKey`, NOT the leaf parser — this was the first bug found and fixed this session, see below) so N-way AND chains (which the binder nests left-associatively, e.g. `A AND B AND C` → `And(And(A,B),C)`) get flattened at arbitrary depth, not just exactly-2. `LogicType::Or` or anything else → bail.
  - **Naming collision caught by the linker, not the compiler**: first draft named the leaf parser `SplitComparisonExpr`, identical name+signature to an unrelated pre-existing free function in `seqscan_as_indexscan.cpp` (splits `col=const` for index scans) — both non-`static` in `namespace bustub`, so `libbustub.a` got two definitions of the same external symbol → `duplicate symbols` link error. C++ doesn't mangle return types into a plain function's linker symbol, so differing return types didn't save it. Renamed to avoid — free functions added to optimizer `.cpp` files need names that are unique project-wide, not just within the file.
  - Wiring in `OptimizeNLJAsHashJoin`: standard recurse-children-then-clone pattern (same as `nlj_as_index_join.cpp`), `dynamic_cast` to `NestedLoopJoinPlanNode` via `Predicate()` accessor (not raw `predicate_` member), build `HashJoinPlanNode` with `nlj_plan.output_schema_`, `GetLeftPlan()`/`GetRightPlan()` passed through unchanged, moved key vectors, `GetJoinType()`. Falls back to returning `optimized_plan` (recursed-but-unconverted NLJ) on any bail — never the raw un-recursed `plan` param.
- Verified correct by hand-trace (not yet by a passing test, since the executor bug below blocks that): single equality, 2-way AND, N-way nested AND, OR-bail, and the flipped-operand-order case all trace through correctly.

#### The `right_tuple_matched_index_` saga — the hardest bug this project, three attempts to get right, now fixed and verified
First-ever real run of `p3.14-hash-join.slt` hung forever (99% CPU, zero output) on `p3.14-hash-join.slt:20`, `test_simple_seq_1 s1 inner join test_simple_seq_2 s2 on s1.col1 = s2.col1` — the planned "first smoke test."
- **Diagnosis method**: `lldb -p <pid>` needed an interactive permission grant unavailable in this session. Used macOS's built-in `sample <pid> 3 -f /tmp/out.txt` instead to get a call-stack profile of the live hung process — found `HashJoinExecutor::Next` spinning at `hash_join_executor.cpp:164` (`bpm->ReadPage(...)`) via `ArcReplacer`/`unordered_map` churn, with no forward progress, in under a minute. This is now the go-to technique for any future hang — see below.
- **Root cause (attempt 1 → hang)**: `right_tuple_matched_index_` (the cursor that lets a match-list bigger than one batch pause/resume across `Next()` calls) was only reset to `0` on a bucket transition, never when advancing to a new left tuple within the same bucket. `test_simple_seq_1/2` both use `Dist::Serial` for `col1` (confirmed `table_generator.cpp:113`) — a clean 1-to-1 join where every key has exactly 1 match. After left tuple #1 finishes, the index is left at `1`; left tuple #2's match loop condition `1 < right_tuples.size()(1)` is false *before the loop body ever runs*, so `left_partition_tuple_index_` (which only advances inside that body) never moves — infinite loop. Query 1 (mock tables, only 3 matches total) avoided this by luck — its few matches happened to land in different buckets, each getting a fresh per-bucket reset.
- **Attempt 2 → wrong variable, didn't compile**: tried resetting via `right_partition_tuple_count_ = 0;` — that's an unrelated `vector<int>` member (the per-partition tuple counter from `Init()`), not `right_tuple_matched_index_`. `vector<int> = 0` has no valid conversion; confirmed compile error.
- **Attempt 3 → compiled, but wrong operator, still hung**: two of the three reset sites used `right_tuple_matched_index_ += 0;` instead of `= 0;` — a no-op that changes nothing, so the same hang persisted (and would've gotten easier to trigger, not harder, since it compounds across no-match tuples too).
- **Attempt 4 → correct assignment, but wrong output (duplicated rows)**: all three sites correctly used `= 0`, and the hang was gone — but the reset for the match-exhausted case was placed *inside* the `while` loop it was resetting, without breaking out. Since `right_tuples` and `left_tuple` are `const auto` locals bound once before the loop, resetting `right_tuple_matched_index_` back to `0` mid-loop made the loop's own continuation check (`right_tuple_matched_index_ < right_tuples.size()`) true again, re-emitting the same tuple's `right_tuples[0]` over and over until `batch_size` hit `BUSTUB_BATCH_SIZE` (20) — manifested as 20 duplicate copies of one row instead of the correct 3.
- **Final fix (verified working)**: keep the reset assignment `right_tuple_matched_index_ = 0;` in the match-exhausted branch, but add a `break;` right after it (alongside the existing `left_partition_tuple_index_ += 1;`) so the inner `while` exits immediately instead of re-checking its own just-reset condition. The other two reset sites (LEFT-no-match, INNER-no-match `else`) are plain sequential code outside any loop, so `= 0` alone was already correct there — only the loop-internal reset needed the `break`.
- **Lesson for next time a resume-cursor needs resetting inside a loop that reads that same cursor**: resetting a loop-condition variable from inside the loop body doesn't stop the loop by itself — the loop will just re-evaluate its condition against the fresh value. Either `break` immediately after the reset, or move the reset to run only after the loop has already exited.

#### Current state (all confirmed by Read + a real build this session, not assumed)
- **Header** (`hash_join_executor.h`): `HashKey::operator==` and `std::hash<HashKey>::operator()` both correctly `const`-qualified now. Public constants `COUNT_LIMIT_FOR_TUPLES_PARTITION = 4096`, `NUM_PARTITIONS = 8`. Members: `left_partitions_`/`right_partitions_` (`vector<vector<page_id_t>>`), `left_partition_tuple_count_`/`right_partition_tuple_count_` (`vector<int>`, per-partition running counts driving repartition triggers), `cached_right_tuples_` (`unordered_map<HashKey, vector<Tuple>>`, rebuilt fresh per partition bucket in `Next()`), resume cursors `left_partition_bucket_index_`/`left_partition_page_index_`/`left_partition_tuple_index_`/`left_partition_page_size_`/`right_tuple_matched_index_` (all `int`, `-1` used as an "unset" sentinel — this is why they're signed, not `uint16_t`). Helper `MakeOutputTuple(left, right_or_nullptr)` builds the output row.
- **`Init()`**: drains both children into `NUM_PARTITIONS` (8) pages each via `InitHashPages`, then loops `GetIndexesToRepartition` → appends 8 more partition slots + `RehashPartiton`s any oversized ones with an incrementing salt, until none exceed `COUNT_LIMIT_FOR_TUPLES_PARTITION`. No `MAX_DEPTH` cap on identical-key skew — low risk for p3.14/15's data size, not blocking.
- **`Next()`**: streams `left_partitions_` bucket-by-bucket; on entering a new bucket, drains+deletes all its `right_partitions_` pages into `cached_right_tuples_`, then walks left pages/tuples, probing the map and emitting matches (or NULL-pad for LEFT, or skip-and-advance for INNER-no-match). Handles multi-page partitions, multi-match-per-left-tuple batch-boundary pausing (mirrors NLJ's `right_pos_` pattern), and empty (zero-tuple) partition pages without crashing.
- **`intermediate_result_page.h`** — ✅ unchanged, still done.

#### Bugs found and fixed this session (in case of regression — re-check these first if `Next()` ever misbehaves)
Multiple rounds of read-and-verify (no test run yet, all caught by hand-tracing + one compiler pass). In case something regresses, check these spots first:
1. **Both missing `const`s** on `HashKey::operator==`/`std::hash<HashKey>::operator()` — was blocking compilation entirely.
2. **`while (batch_size >= 0 && ...)`** in the match-emission loop — `batch_size` is `size_t` (unsigned), so `>= 0` is tautologically true and ignores the batch limit entirely, plus underflows on decrement past 0. Fixed to `> 0`.
3. **INNER-join-no-match infinite loop** — the original `if (match) {...} else if (LEFT) {...}` had no `else`, so a plain INNER left tuple with no match never advanced its cursor. Fixed by adding `else { left_partition_tuple_index_ += 1; }`.
4. **Double-indexing typo**: `left_partitions_[left_partition_page_index_][left_partition_page_index_]` (same var used for both dims) → fixed to `[left_partition_bucket_index_][left_partition_page_index_]`.
5. **Off-by-one on the outer bucket-exhaustion check**: `(bucket_index_+1) > left_partitions_.size()` let `bucket_index_` overshoot to exactly `size()` before breaking, causing an out-of-bounds `right_partitions_[size()]` access right after draining the last bucket (this fired on **every completed query**, not just an edge case). Fixed `>` → `>=`.
6. **Zero-tuple partition pages** (very plausible — `InitHashPages` always allocates one page per partition even if it gets zero tuples, and with only 8 fixed partitions a small test table can easily leave one empty): needed a dedicated `if (left_partition_page_size_ == 0) { reset sentinels; page_index_ += 1; continue; }` branch, added in two steps — first pass forgot to reset the sentinels back to `-1` before `continue`, which left stale `0`/`0` state that caused a *second*, unwanted page-index advance on the next loop iteration (silently skipping a real page, or overshooting out of bounds if the empty page was last in its bucket).
7. **Same root cause, different spot**: the bucket-transition detector used strict equality (`page_index_+1 == bucket.size()`) to mean "just finished the last page" — but the zero-page skip above can push `page_index_` **past** `size()-1` in one jump (e.g., a single-page bucket that's entirely empty), which the `==` check can't catch. Fixed `==` → `>=`, matching fix #5's pattern. This was the trickiest one — took two attempts to find because fix #6 alone looked sufficient until traced against a single-page-empty-bucket scenario specifically.
8. **`MakeOutputTuple`'s LEFT-join null-pad used `Value{}`** (default ctor = `TypeId::INVALID`, confirmed in `type/value.h:70`) instead of a properly-typed NULL. Fixed to `ValueFactory::GetNullValueByType(right_tuple_schema.GetColumn(i).GetType())`, same pattern as NLJ's `ConstructOutTuple`.
9. **`RehashPartiton` swapped join-key-expression args** in `Init()`'s repartition loop (right-side rehash was using `LeftJoinKeyExpressions()` against right tuples and vice versa, plus both calls wrote to `left_partition_tuple_count_`) — fixed across two passes, now correctly `RightJoinKeyExpressions()`/`right_partition_tuple_count_` for the right call and `LeftJoinKeyExpressions()`/`left_partition_tuple_count_` for the left call. Only matters once repartitioning actually triggers (>4096 tuples in one partition) — unlikely for p3.14/15's data, but was wrong regardless.
10. **Orphaned page leak in `RehashPartiton`** — old pages were detached from the partition vector but never `bpm->DeletePage()`'d. Fixed with a cleanup loop at the end.
11. `MakeOutputTuple`'s schema locals changed from `const auto` (copies the `Schema`) to `const auto &` (minor perf, not correctness).

**Still open, low priority, cosmetic only — not blocking, both tests pass despite this**: NULL-key right tuples are still inserted into `cached_right_tuples_` unfiltered instead of being skipped before insertion — harmless because `HashKey::operator==` already makes any NULL-containing key compare unequal to everything (including itself), so it can never be matched; just wastes a bit of map space.

#### The two-hash mental model (still the crux — internalize this)
- **Partition hash** (`GetHashPartitionIndex`, salted by `repartition_salt`) is COARSE — job is to make equal keys co-locate in the same partition index, not to distinguish all keys perfectly. **Map hash** (`std::hash<HashKey>` + `operator==`, used inside `cached_right_tuples_`) is EXACT — job is to distinguish keys within one already-loaded partition. Don't conflate them; `Next()` does a plain `map.find(k)` lookup, never re-applies `GetHashPartitionIndex`.
- **Co-location invariant**: a partition is a mixed bag of many distinct keys that collided under `% NUM_PARTITIONS` — the only guarantee is `K_L == K_R ⇒ part(K_L) == part(K_R)`, so a left tuple's matches are always in the same-index right partition.

- **Tests**: p3.14-hash-join.slt ✅ PASS, p3.15-multi-way-hash-join.slt ✅ PASS (both verified 2026-07-20, exit code 0, zero `wrong result` occurrences). Isolate a slice of a `.slt` file for faster iteration when debugging: `sed -n '1,31p' test/sql/p3.14-hash-join.slt > /tmp/mini.slt` then run that instead of the whole file. Run command: `build/bin/bustub-sqllogictest <test.slt> --verbose -d --in-memory` (from `build/`, or prefix with `../test/sql/` for the path if already `cd`'d into `build/`). Build command confirmed working this session: `cd build && make -j$(sysctl -n hw.ncpu) sqllogictest` (macOS has no `nproc`).
- **Debugging a hang**: `lldb -p <pid>` needs an interactive permission grant on macOS that isn't available in an agent session — don't rely on it. Instead use `sample <pid> <seconds> -f /tmp/out.txt` (built into macOS) to get a call-stack profile of a live process — this is what found the bug above, pointing straight at the spinning line in under a minute. `ps aux | grep <name>` to find the PID first; run the hanging command with `&` to background it locally (separate from the harness's own background-task mechanism) so you can `sample` it before killing it.
- **Verify tool caveat**: `WebFetch` fails on the course site with a TLS cert error; use `curl -sL <url>` instead.

### ExternalMergeSort — ✅ DONE. p3.16-sort-limit.slt passes end-to-end, zero failures
> **Verified 2026-07-26** by actually running the test to completion: `./bin/bustub-sqllogictest ../test/sql/p3.16-sort-limit.slt --verbose -d --in-memory` → exit 0, 0 `wrong result` occurrences. Covers the 100,000-row `__mock_external_merge_sort_input` table, multi-column `ORDER BY` with mixed `ASC`/`DESC`, all `NULLS FIRST`/`NULLS LAST`/default combinations, and nested subqueries with `LIMIT`. Built via pair-programming (user wrote the code, reviewed/verified after every edit) — 10 distinct bugs found and fixed across several review passes. Depends on `TupleComparator`/`GenerateSortKey` (see `execution_common.cpp` section above) and `IntermediateResultPage` (Task #3, already done).

#### Design: run-generation chunk size = `BUSTUB_BATCH_SIZE` (deliberate choice, not a hard requirement)
No memory-budget constant or grading check pins down how large an in-memory sort chunk should be before spilling it as one run — confirmed by research: nothing in the codebase or `.slt` `ensure:` checks constrains this. Deliberately chose **`BUSTUB_BATCH_SIZE` (20)**, not a page-sized chunk (~500-680 tuples for small schemas), specifically because a large chunk would make most of `p3.16`'s small mock tables produce exactly 1 run each — meaning the merge-round logic would never be exercised except by the one giant 100k-row query. A small chunk forces multiple runs (and therefore real merge rounds) even on tiny tables, surfacing merge bugs immediately instead of only on the slow, large query. Tradeoff: more runs → more `log2(N)` merge rounds → more total I/O than a page-packed design — acceptable since nothing grades performance and the test harness uses an in-memory disk manager.

#### Current architecture (all confirmed by Read + a real passing test run, not assumed)
- **`MergeSortRun`** (`external_merge_sort_executor.h`): holds `pages_` (`vector<page_id_t>`) + `bpm_`. Move-only (declares `MergeSortRun(MergeSortRun&&) noexcept = default;` + move assignment — this implicitly **deletes** the copy constructor/assignment per standard C++ rules, so every place a `MergeSortRun` crosses ownership needs an explicit `std::move`). `InsertTuple(tuple)`: appends to the current last page, spilling to a freshly-allocated new page when full (loop-until-fits pattern, same as `HashJoinExecutor`'s partition spill). `DeleteAllPages()`: caller-driven cleanup once a run's data has been fully consumed by a merge (mirrors the "orphaned page leak" lesson from `HashJoinExecutor`'s `RehashPartiton` bug).
- **`MergeSortRun::Iterator`**: position state = `page_idx_` + `tuple_idx_` (both `size_t`, default-initialized to `0`). Canonical "end" value = `page_idx_ == run_->pages_.size()` (mirrors `IndexIterator`'s `page_id_ == INVALID_PAGE_ID` sentinel from Project 2) — `Begin()`/`End()` just construct via the private `Iterator(const MergeSortRun*)` ctor (accessible from `MergeSortRun`'s member functions via nested-class access rules, not the `friend class MergeSortRun;` declaration — that friend declaration grants the *reverse* access instead) and set `page_idx_` accordingly. `operator++`: bumps `tuple_idx_`, rolls over to the next page when `tuple_idx_ >= current_page->GetNumTuples()`. Every page in a run is guaranteed non-empty by construction (a page only gets allocated at the moment a tuple is actually being written to it, never pre-allocated) — this sidesteps the zero-tuple-page bug class `HashJoinExecutor` had to special-case.
- **`ExternalMergeSortExecutor::Init()`**: for each `BUSTUB_BATCH_SIZE`-sized chunk read from the child, builds `SortEntry`s via `GenerateSortKey`, `std::sort`s the chunk with `cmp_`, then spills the *entire sorted chunk* as **exactly one new run** (`merge_sort_runs_.emplace_back(...)` once per chunk, then `merge_sort_runs_.back().InsertTuple(...)` per tuple — mutating the vector's actual element directly, never a disconnected local copy). After the child is exhausted, calls `RecursiveMerge()` to collapse all runs down to one, then seeds `it_ = merge_sort_runs_[0].Begin()`.
- **`RecursiveMerge()`**: pairs up runs at index `i`/`i+1` for `i` stepping by 2 across the *entire* range `[0, size())`; an unpaired leftover index carries over untouched to the next round via `std::move`. Recurses until `size() <= 1`.
- **`MergeTwoRuns(r1, r2)`**: classic two-sorted-sequence merge via `Iterator`s — re-derives each side's `SortEntry` via `GenerateSortKey` on every comparison (pages store raw tuples only, no keys), drains whichever side survives once the other hits `End()`.
- **`Next()`**: streams `it_` into the output batch, capped by `batch_size`, returns `!tuple_batch->empty()`.

#### Bugs found and fixed this session (in case of regression — check these first)
1. **Constructor never assigned `plan_`** — the init list set `cmp_`/`child_executor_` but omitted `plan_(plan)` entirely, leaving a raw pointer member uninitialized. Every later `plan_->GetOrderBy()`/`plan_->OutputSchema()` call was UB. Caught before ever running — by reading the diff, not by a crash.
2. **`MergeTwoRuns` fell off the end without `return msr_result;`** — a non-`void` function returning nothing is UB; the "returned" `MergeSortRun` would have been garbage, corrupting every subsequent merge round.
3. **The big one — `Init()` mutated a disconnected local copy instead of the vector's actual element.** Original pattern: keep a local `curr_merge_run` variable, `InsertTuple` into it across many iterations, and only `emplace_back(curr_merge_run)` at rare "transition" points. Since `emplace_back` constructs an **independent** copy/move at the instant it's called, every run that ended up in `merge_sort_runs_` was snapshotted while still empty (either at the very first push, or immediately after being replaced with a fresh empty run and *before* the next tuple was inserted into it). Net effect: `merge_sort_runs_` filled up with permanently-empty runs, and every tuple's data was silently lost each time a "new run" transition discarded the old, now-populated local variable without ever having pushed its populated state anywhere. **Fix**: stop keeping a separate local variable — mutate `merge_sort_runs_.back()` directly, and only `emplace_back` a fresh run at genuine transition points.
4. **Inverted success/failure check — twice, independently.** `IntermediateResultPage::InsertTuple`'s doc comment confirms `TRUE` = success, `FALSE` = page full (verified by reading the header, not assumed). Both `MergeSortRun::InsertTuple` and (separately) `Init()`'s page-limit check had `if (InsertTuple(...))` treating success as the "page full, start new run" signal — backwards. Consequence: successful inserts triggered spurious duplicate-insert-into-a-new-page, and genuine failures were silently dropped. Fixed both call sites to `if (!InsertTuple(...))`.
5. **A more subtle design bug in the same area, found *after* fixing #3/#4**: reusing a page-capacity check (`InsertTupleWithOnePageLimit`, capped at 2 pages) to decide when to start a new run meant a single run could span **multiple independently-`std::sort`'d chunks** (since 20 tuples/chunk is far smaller than a 2-page capacity of ~1000+ tuples). Two independently-sorted chunks concatenated into one run is *not* a globally-sorted run — it silently violates the invariant `MergeTwoRuns` depends on. **Fix**: a run must correspond to exactly one sort chunk; always start a brand-new run at the top of each outer-loop iteration, and use unlimited `InsertTuple` (which already spans however many pages one chunk needs) instead of a page-capped variant. `InsertTupleWithOnePageLimit` became dead code after this fix and was deleted.
6. **`RecursiveMerge`'s loop bound was off-by-one**: `for (i = 0; i < merge_sort_runs_.size() - 1; i += 2)` never lets `i` reach the last unpaired index when the run count is odd, making the `else` (carry-over-the-leftover) branch permanently unreachable dead code — the last run of an odd-sized list was silently dropped every recursion level where the count was odd (common, not a rare edge case, given a run count that rarely stays a power of 2 across every halving). Fixed to `i < merge_sort_runs_.size()`, letting the existing inner `if (i+1 < size())`/`else` correctly decide pair-vs-carry-over.
7. **Move-only `MergeSortRun` — several copy attempts wouldn't compile.** Declaring a move ctor/assignment implicitly deletes the copy ctor/assignment (standard C++ rule). Every place a `MergeSortRun` (or `vector<MergeSortRun>`) got assigned/pushed from an existing lvalue needed an explicit `std::move`: `RecursiveMerge`'s `r1`/`r2` locals, the odd-leftover `emplace_back`, and the `merge_sort_runs_ = new_level_merge_sort_runs;` vector assignment (→ `std::move(new_level_merge_sort_runs)`).
8. **`Next()` ignored `batch_size` entirely** — the streaming loop had no cap tied to `batch_size`, so the *first* call would drain the entire final run (all 100,000 rows, for the big table) into one batch regardless of what the caller asked for. Fixed by adding `&& tuple_batch->size() < batch_size` (implemented as a decrementing local `batch_size` counter) to the loop condition, matching the standard batch pattern used everywhere else in this codebase.
9. **Missing explicit template instantiation** — `template class ExternalMergeSortExecutor<2>;` was absent from the bottom of the `.cpp` (likely dropped during an earlier edit). Since the class's methods are defined out-of-line in the `.cpp`, omitting this meant the compiler never emitted actual object code for `ExternalMergeSortExecutor<2>` in this translation unit — everything compiled fine (declarations visible via the header), but linking failed with "symbol(s) not found for architecture arm64" in `executor_factory.cpp`'s `make_unique<ExternalMergeSortExecutor<2>>(...)` call. Pure link-time symptom, no logic bug — easy to miss since nothing about the header/declarations looks wrong.
10. **`-Wreorder` as a hard build failure**: constructor init list wrote `cmp_(...), plan_(plan), ...` — order-written differs from declaration order (`plan_`, `cmp_`, `child_executor_` in the header). C++ always initializes in declaration order regardless of list-write order, so this wasn't a correctness bug (nothing here depends on the other), but this project's build treats `-Wreorder-ctor` as `-Werror`, turning it into an actual compile failure. Fixed by matching the init list order to declaration order.

#### Key lessons for the next executor with move-only resource-owning types or page-spill logic
- **A local variable is not a substitute for the container element it's meant to represent.** If you need to keep mutating "the current X," mutate `container.back()` (or hold an index/reference into the container) — don't keep a separate local copy and try to `push_back`/`emplace_back` it at "transition points." The copy diverges from the container's element the instant it's constructed.
- **Read the actual return-value convention of a page/data-structure method before writing an `if` around it** — `true`/`false` conventions are not universally "success"/"failure" in one fixed direction across every method in a codebase; `IntermediateResultPage::InsertTuple`'s doc comment was the ground truth here, not intuition.
- **A "run"/"partition"/"chunk" invariant (e.g., "must be fully sorted end-to-end") needs to be checked against *every* code path that writes into it**, not just the one you're currently focused on — the run/chunk-boundary bug (#5) was only found by re-examining the *design*, not by re-reading the same lines for syntax errors again.
- **Declaring a move constructor/assignment silently deletes copy operations** — grep for lvalue-passing `emplace_back`/`operator=` calls on that type after adding move ops; the compiler will catch most of these as errors, but not all call sites necessarily get exercised/compiled in the same pass depending on template instantiation timing.
- **A class template's out-of-line method definitions need an explicit instantiation line to actually produce linkable object code** — a "compiles fine, fails to link" symptom for a templated executor almost always means this line is missing or was accidentally deleted, not a logic bug in the methods themselves.

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

## Task #4 — Code Recon (verified from source 2026-07-21, ready to implement)

### `execution_common.{h,cpp}` — TupleComparator + GenerateSortKey — ✅ DONE (2026-07-25, build green)
- Types (`execution_common.h`): `using SortKey = std::vector<Value>;` `using SortEntry = std::pair<SortKey, Tuple>;` `OrderBy = std::tuple<OrderByType, OrderByNullType, AbstractExpressionRef>` (from `binder/bound_order_by.h` — **not** `binder/table_ref/bound_order_by.h`). `OrderByType{INVALID=0, DEFAULT=1, ASC=2, DESC=3}`, `OrderByNullType{DEFAULT=0, NULLS_FIRST=1, NULLS_LAST=2}`. `TupleComparator` ctor `explicit TupleComparator(std::vector<OrderBy> order_bys)` moves into `order_bys_` member.
- **`OrderByType::INVALID` is provably unreachable at execution time** — `Binder::BindSort` (`bind_select.cpp:930-947`) only ever produces `DEFAULT`/`ASC`/`DESC`; anything else hits `throw NotImplementedException("unimplemented order by type")` during binding, before a plan is ever built. So `BUSTUB_ASSERT(ob_type != OrderByType::INVALID, ...)` inside the comparator is a safe, correct assert (documents the invariant, doesn't need a real handling branch).
- **NULL-ordering convention, confirmed against `p3.16-sort-limit.slt` test data (not assumed)**: `Value::Compare*` methods (`integer_parent_type.cpp`, `bigint_type.cpp`, etc.) all return `CmpBool::CmpNull` — never `CmpTrue`/`CmpFalse` — when either operand `IsNull()`. That's unusable for a sort comparator (which needs a definite `bool`), so NULLs must be special-cased *before* calling `Compare*`, never fed into it. The exact rule, derived from `order by colH limit 3` (implicit ASC, no `NULLS` clause) returning NULL rows first, and `order by colG asc nulls last` pulling real values to the top:
  - `NULLS_FIRST` → null always sorts before every non-null, regardless of ASC/DESC.
  - `NULLS_LAST` → null always sorts after every non-null, regardless of ASC/DESC.
  - `DEFAULT` (no `NULLS` keyword) → null behaves like the smallest possible value and participates in normal direction logic: `DEFAULT+ASC`/`DEFAULT+DEFAULT-direction` → null first; `DEFAULT+DESC` → null last.
- **Final correct implementation** (`execution_common.cpp`):
  ```cpp
  auto TupleComparator::operator()(const SortEntry &entry_a, const SortEntry &entry_b) const -> bool {
    const auto& [sk_a, t_a] = entry_a;
    const auto& [sk_b, t_b] = entry_b;
    for (size_t i = 0; i < order_bys_.size(); i += 1) {
      const auto& [ob_type, ob_null_type, expr_ref] = order_bys_[i];
      BUSTUB_ASSERT(ob_type != OrderByType::INVALID, "Order by type can not be invalid");
      const auto &va = sk_a[i];
      const auto &vb = sk_b[i];
      if (va.IsNull() || vb.IsNull()) {
        if (va.IsNull() && vb.IsNull()) { continue; }  // both null, tie, move next
        auto is_null_first = (ob_null_type == OrderByNullType::NULLS_FIRST ||
                              (ob_null_type == OrderByNullType::DEFAULT &&
                               (ob_type == OrderByType::ASC || ob_type == OrderByType::DEFAULT)));
        return va.IsNull() ? is_null_first : !is_null_first;
      }
      if (auto e_cmp = va.CompareEquals(vb); e_cmp == CmpBool::CmpTrue) { continue; }
      if (auto le_cmp = va.CompareLessThan(vb); le_cmp == CmpBool::CmpTrue) {
        return ob_type == OrderByType::ASC || ob_type == OrderByType::DEFAULT;
      }
      if (auto ge_cmp = va.CompareGreaterThan(vb); ge_cmp == CmpBool::CmpTrue) {
        return ob_type == OrderByType::DESC;
      }
    }
    return false;  // tied on every column
  }

  auto GenerateSortKey(const Tuple &tuple, const std::vector<OrderBy> &order_bys, const Schema &schema) -> SortKey {
    auto result_sortkey = SortKey{};
    for (const auto &ob : order_bys) {
      auto [ob_type, ob_null_type, expr_ref] = ob;
      result_sortkey.emplace_back(expr_ref->Evaluate(&tuple, schema));
    }
    return result_sortkey;
  }
  ```
- **Bug history — the null-handling branch went through 4 broken iterations before landing on the one-liner above** (kept here since the failure modes are subtle and easy to reintroduce):
  1. **v1**: only handled `va.IsNull()` returning `true`, with a copy-paste typo (`ob_type == ASC || ob_type == ASC` instead of `|| ob_type == DEFAULT`) — silently broke the single most common case (plain `ORDER BY col`, no keywords, table has nulls).
  2. **v2**: added an unconditional second `if` for the `NULLS_LAST`/`DEFAULT+DESC` case, but without gating it on `vb.IsNull()` — it fired regardless of which side actually held the null. Under `NULLS_LAST`, comparing `(NULL, 5)` AND `(5, NULL)` **both returned `true`** — a direct antisymmetry violation (`comp(a,b) && comp(b,a)` both true), which is undefined behavior for `std::sort`/heaps.
  3. **v3**: added the missing `vb.IsNull()` guard, fixing the single-column case, but still had no `else`/fallthrough guard — when neither inner `if` fired, execution fell through into `CompareEquals`/`CompareLessThan`/`CompareGreaterThan` with a null operand (all silently return `CmpNull`, matching no `== CmpTrue` check), causing the loop to treat a null-vs-nonnull pair as a **tie** and incorrectly defer the decision to the *next* ORDER BY column. Concretely broke multi-column sorts like `ORDER BY colA NULLS LAST, colB ASC` — a row with `colA=NULL` could out-rank a row with real `colA` based on `colB`, which should never be consulted.
  4. **v4 (final)**: collapsed both branches into the single `is_null_first`/`!is_null_first` ternary shown above — computed once, returns immediately and unconditionally whenever exactly one side is null, with no fallthrough path. Verified by hand-trace against both the `NULLS_LAST` antisymmetry case and the multi-column `colA NULLS LAST, colB ASC` case.
- **Structured-binding perf note**: `operator()` runs O(n log n) times during a sort — bind with `const auto &[...]` (not `auto [...]`) for `entry_a`/`entry_b`/`order_bys_[i]` to avoid copying the full `Tuple`/`SortKey`/shared_ptr on every comparison. (`GenerateSortKey`'s per-tuple, once-per-column `auto [ob_type, ob_null_type, expr_ref] = ob;` is a much lower-stakes copy — left as-is, not worth blocking on.)
- File has a comment: "Above are all you need for P3. You can ignore the remaining part of this file until P4" — everything below that line in the file is P4/MVCC, not relevant here.

### `external_merge_sort_executor.{h,cpp}` + `sort_plan.h` — ✅ DONE, see dedicated "ExternalMergeSort" section above for full implementation + bug history
- `SortPlanNode` (`plans/sort_plan.h`): `GetChildPlan()`, `GetOrderBy() -> const std::vector<OrderBy>&`. Only one child.
- Header (`external_merge_sort_executor.h`): templated `ExternalMergeSortExecutor<K>` — **only `K=2` is instantiated** (`template class ExternalMergeSortExecutor<2>;` at bottom of .cpp — MUST be present or the build links but fails at symbol resolution) → strict 2-way merge sort.
- Final design implemented: run-generation chunk size = `BUSTUB_BATCH_SIZE`, one run per sort chunk (never spanning multiple independently-sorted chunks), `RecursiveMerge()` pairs up runs across the full index range each round (carrying over an odd leftover), `MergeTwoRuns` does the classic two-sorted-sequence merge re-deriving `SortEntry`s via `GenerateSortKey` on each comparison since pages store raw tuples only.

### `limit_executor.{h,cpp}` + `limit_plan.h` — trivial, do this first in Task #4
- `LimitPlanNode`: `GetLimit() -> size_t`, `GetChildPlan()`.
- `.cpp`: constructor, `Init()`, `Next()` all `UNIMPLEMENTED("TODO(P3): Add implementation.")`. Simplest executor in Task #4 — just needs a running count member, cap output at `plan_->GetLimit()`, otherwise pass child batches straight through (truncate the last batch as needed).

### `topn_executor.{h,cpp}` + `topn_plan.h` + `sort_limit_as_topn.cpp`
- `TopNPlanNode`: `GetN() -> size_t`, `GetOrderBy() -> const std::vector<OrderBy>&`, `GetChildPlan()`.
- `.cpp` stubs use `NotImplementedException` (not `UNIMPLEMENTED`) — constructor is an empty `{}` (doesn't store `plan_`/child), `Init()` throws, `Next()` `return false`, `GetNumInHeap()` throws. `GetNumInHeap()` is graded (`ensure:` checks in `.slt` likely assert heap size ≤ N at some point) — must track a real heap/container sized to `plan_->GetN()`, not just a sorted vector, if the check inspects size mid-scan.
- **`sort_limit_as_topn.cpp` is currently a total no-op**: `auto Optimizer::OptimizeSortLimitAsTopN(const AbstractPlanNodeRef &plan) -> AbstractPlanNodeRef { // TODO(student): implement sort + limit -> top N optimizer rule \n return plan; }`. Needs: recurse children first (standard pattern), then check if `plan` is `LimitPlanNode` whose child is `SortPlanNode` → replace with `TopNPlanNode(output_schema, sort_child->GetChildPlan(), sort_child->GetOrderBy(), limit_plan->GetLimit())`. Tested by `p3.17-topn.slt`.
- Design: classic top-N via a heap of size N (bounded), or maintain a sorted structure and evict the worst when exceeding N — reuse `TupleComparator`/`GenerateSortKey` from `execution_common.cpp`. Since it's a pipeline breaker, build fully in `Init()`, emit in `Next()` (mirrors Aggregation/HashJoin pattern already used elsewhere in this codebase).

### `window_function_executor.{h,cpp}` + `window_plan.h` + `topn_per_group_executor.{h,cpp}`
- `WindowFunctionPlanNode` (`plans/window_plan.h`): `WindowFunctionType` enum = `{CountStarAggregate, CountAggregate, SumAggregate, MinAggregate, MaxAggregate, Rank}` — same 5 aggregate types as regular Aggregation PLUS `Rank`. Each `WindowFunction` struct has `function_` (the aggregate expr), `type_`, `partition_by_` (vector of exprs), `order_by_` (vector of `OrderBy`).
- Frame semantics per header comment: **with ORDER BY** → frame = UNBOUNDED PRECEDING to CURRENT ROW (running/cumulative aggregate); **without ORDER BY** → frame = UNBOUNDED PRECEDING to UNBOUNDED FOLLOWING (whole-partition aggregate, same value repeated for every row in the partition). This matches the project notes ("frame: with ORDER BY = first-to-current, without = entire partition").
- `.cpp` current state: constructor DOES store `plan_`/`child_executor_` (already correct, unlike TopN's empty ctor) — `Init()` throws `NotImplementedException`, `Next()` returns `false`.
- RANK ties: enum has no special metadata: standard SQL `RANK()` semantics expected — tied rows (equal ORDER BY key) get the same rank, and the next rank skips by the number of ties (e.g. 1,1,3, not 1,1,2). No explicit tie-breaking hint found in code — implement standard RANK behavior and verify against `p3.20-window-function.slt`.
- Design: for each output row, need to know its partition (via `MakeAggregateKey`-style grouping on `partition_by_` — can likely reuse `AggregateKey`/`std::hash<AggregateKey>` from `plans/aggregation_plan.h`) and its position within the partition's ORDER BY. Likely approach: buffer all child tuples in `Init()`, group by partition key, sort each partition by `order_by_` (reuse `TupleComparator`), then compute running aggregate/rank per partition, finally re-emit rows in ORIGINAL input order (window functions don't reorder output — they add a computed column) — need to track original tuple order/index separately from the sort-for-computation order.
- **`topn_per_group_executor.{h,cpp}` is a SEPARATE, unrelated executor** (not used by window functions) — has its own `TopNPerGroupPlanNode` with `GetOrderBy()`, `GetGroupBy()`, `GetN()` (top-N per group, e.g. "top 3 scores per player" for leaderboard queries). Same stub shape as WindowFunction: ctor stores plan+child, `Init()` throws `NotImplementedException`, `Next()` returns `false`. Relevant tests: `p3.leaderboard-q1/q1-index/q1-window/q2/q3.slt`.
- Test files confirmed present: `test/sql/p3.20-window-function.slt` (2.0K), `p3.leaderboard-q1-window.slt` (525B), plus q1/q1-index/q2/q3 variants.

**Suggested Task #4 order** (easiest → hardest, matches dependency chain): `Limit` ✅ done → `TupleComparator`+`GenerateSortKey` ✅ done → `ExternalMergeSort` ✅ done (`p3.16-sort-limit.slt` passes) → **`sort_limit_as_topn` optimizer + `TopN` executor (next milestone)** → `WindowFunction` (+ `TopNPerGroup` if in scope). Tests: `p3.16-sort-limit` ✅, `p3.17-topn`, `p3.18/19-integration`, `p3.20-window-function`, `p3.leaderboard-*`.

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
