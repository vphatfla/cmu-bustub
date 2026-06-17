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
| Task #1 | Access Method Executors (SeqScan, Insert, Update, Delete, IndexScan, optimizer) | ⬜ TODO |
| Task #2 | Aggregation & Join Executors (Aggregation, NLJ, NestedIndexJoin) | ⬜ TODO |
| Task #3 | Hash Join & Optimization (IntermediateResultPage, HashJoin, NLJ→HashJoin optimizer) | ⬜ TODO |
| Task #4 | Sort, Limit & Window Functions (ExternalMergeSort, Limit, WindowFunction) | ⬜ TODO |

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
