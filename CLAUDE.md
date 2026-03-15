# BusTub Working Context

> **CLAUDE CODE DIRECTIVE:** Automatically update this context file as you work. Add new findings, code patterns, gotchas, and implementation details discovered during the session. Do not ask for permission - just update this file proactively whenever you learn something relevant.
>
> **MANDATORY:** Always re-read the relevant source files (using the Read tool) before answering any question about the codebase. Never rely on previously cached file contents — the user may have edited files between questions.

## Project: CMU 15-445 - Project 2: B+ Tree Index

**Due:** Oct 26, 2025 @ 11:59pm

---

## Tasks Overview

| Task | Description | Status |
|------|-------------|--------|
| Task #1 | B+Tree Pages (Base, Internal, Leaf) | ✅ DONE |
| Task #2 | B+Tree Operations (Insert, Delete, Search) | ✅ DONE |
| Task #3 | Index Iterator | ✅ DONE |
| Task #4 | Concurrency Control (latch crabbing) | 🔄 IN PROGRESS (MixTest1/MixTest2 failing) |

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                    BPlusTree (b_plus_tree.h/cpp)            │
│         Insert() / Remove() / GetValue() / Begin()         │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                      Page Classes                           │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────┐ │
│  │ BPlusTreePage   │  │ InternalPage    │  │ LeafPage    │ │
│  │ (base class)    │◄─┤ (keys + ptrs)   │  │ (keys+vals) │ │
│  └─────────────────┘  └─────────────────┘  └─────────────┘ │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                   BufferPoolManager                         │
│            FetchPage() / NewPage() / UnpinPage()           │
└─────────────────────────────────────────────────────────────┘
```

---

## File Locations

| Component | Header | Source |
|-----------|--------|--------|
| Base Page | `src/include/storage/page/b_plus_tree_page.h` | `src/storage/page/b_plus_tree_page.cpp` |
| Internal Page | `src/include/storage/page/b_plus_tree_internal_page.h` | `src/storage/page/b_plus_tree_internal_page.cpp` |
| Leaf Page | `src/include/storage/page/b_plus_tree_leaf_page.h` | `src/storage/page/b_plus_tree_leaf_page.cpp` |
| B+ Tree | `src/include/storage/index/b_plus_tree.h` | `src/storage/index/b_plus_tree.cpp` |
| Iterator | `src/include/storage/index/index_iterator.h` | `src/storage/index/index_iterator.cpp` |
| Header Page | `src/include/storage/page/b_plus_tree_header_page.h` | - |
| Printer | `tools/b_plus_tree_printer/b_plus_tree_printer.cpp` | - |

---

## Current State (Session 2026-03-15)

### Test Results
| Test | Status |
|------|--------|
| `b_plus_tree_insert_test` (4 tests) | ✅ ALL PASS |
| `b_plus_tree_delete_test` (3 tests) | ✅ ALL PASS |
| `BPlusTreeConcurrentTest.InsertTest1` | ✅ PASS |
| `BPlusTreeConcurrentTest.InsertTest2` | ✅ PASS |
| `BPlusTreeConcurrentTest.DeleteTest1` | ✅ PASS |
| `BPlusTreeConcurrentTest.DeleteTest2` | ✅ PASS |
| `BPlusTreeConcurrentTest.MixTest1` | ❌ FAIL (separator key corruption) |
| `BPlusTreeConcurrentTest.MixTest2` | ❌ FAIL (same root cause) |

### MixTest1 Failure Pattern
- Test inserts 500 odd keys, then concurrently inserts 500 even keys + deletes 500 odd keys
- After all operations, tree contains some ODD keys that should have been deleted
- Root cause: **separator keys in internal pages become inconsistent with children's actual key ranges**
- Traversal goes to wrong leaf → delete silently misses the key
- Verified by adding traversal logging: `key=1, leaf_range=[2, 4]` — key 1 is routed to leaf [2, 4] instead of its correct leaf
- **Bug persists even with pessimistic-only paths** (optimistic disabled) → logic bug, not concurrency race

---

## Bugs Fixed This Session (2026-03-15)

### BUG 1 (FIXED): RedistributeInternalPageLeftSibling — `ShiftKeyAndValueRight(1)` should be `ShiftKeyAndValueRight(0)`

**Root Cause:** `ShiftKeyAndValueRight(1)` shifts entries from index 1 rightward, leaving `ValueAt(0)` in place. Then `SetValueAt(0, borrowed_ptr)` overwrites it. But the old `ValueAt(0)` (leftmost child pointer) is LOST — it should have been shifted to position 1. Instead, `ValueAt(1)` keeps its old value AND `ValueAt(2)` has the shifted copy → duplicate child pointer.

**Fix:** Changed to `ShiftKeyAndValueRight(0)` which shifts ALL entries (including `ValueAt(0)`) rightward, preserving all child pointers.

**Verification:** Added `ValidateInternalPage` assertion that checks for duplicate `ValueAt` entries. Confirmed the corruption at `"RedistributeInternalLeft:curr"` before fix, and no corruption after fix.

**Location:** `b_plus_tree.cpp` in `RedistributeInternalPageLeftSibling`

### BUG 2 (FIXED): Iterator constructor deadlock — double read-latch on same page

**Root Cause:** `Begin()` and `Begin(key)` held a `ReadPageGuard` on the leaf page in `ctx.read_set_` while constructing the iterator. The iterator constructor called `LoadPageAndIterator` → `bpm_->ReadPage(page_id)` on the SAME page. Since `std::shared_mutex` is not reentrant, the same thread acquiring a second read latch deadlocks (especially with writer-preference on macOS where a waiting writer blocks new readers).

**Fix:** Changed iterator constructor to accept a `ReadPageGuard` directly (moved from `ctx.read_set_`). Constructor signature:
```cpp
IndexIterator(shared_ptr<TracedBufferPoolManager> bpm, const KeyComparator &comparator,
              ReadPageGuard leaf_guard, page_id_t page_id, const optional<KeyType> &key);
```
- `Begin()` moves guard from `ctx.read_set_` into iterator
- `End()` passes default-constructed `ReadPageGuard{}` + `INVALID_PAGE_ID`
- Constructor body sets up `leaf_page_`, tombstone set, binary search inline (same logic as `LoadPageAndIterator` but without `bpm_->ReadPage()`)

**Files changed:** `index_iterator.h`, `index_iterator.cpp`, `b_plus_tree.cpp` (5 construction sites)

### BUG 3 (FIXED): LoadPageAndIterator recursive → iterative

**Root Cause:** When all entries on a leaf page are tombstoned, `LoadPageAndIterator` recursively calls itself to advance to the next sibling page. Deep recursion with many consecutive tombstoned pages could overflow the stack.

**Fix:** Replaced tail recursion with a `while(true)` loop.

### BUG 4 (FIXED): RedistributeLeafPageLeftSibling — no tombstone eviction before shift

**Root Cause:** When a leaf is logically underfull but physically full (due to tombstones), `ShiftKeyAndValueRight(0)` asserts `GetSize() < GetMaxSize()`. The assertion fails because physical size == max_size.

**Fix:** Added tombstone eviction loop before the shift:
```cpp
while (curr_page->GetSize() >= curr_page->GetMaxSize() && curr_page->GetTombstonesSize() > 0) {
  curr_page->DeleteOldestKeyInTombstones();
}
```

### BUG 5 (FIXED): Dead page detection for optimistic paths

**Root Cause:** When a pessimistic Remove merges a leaf, the dead page's write lock is released. A concurrent optimistic operation that cached the dead page's page_id can then write-lock and modify it — writing to a page no longer in the tree.

**Fix:**
1. After `MergeTwoLeafPages`, mark the dead page as `INVALID_INDEX_PAGE`: `page->SetPageType(IndexPageType::INVALID_INDEX_PAGE)`
2. In `InsertOptimistic` and `RemoveOptimistic`, after `OptimisticTraverseNode` returns, check `IsLeafPage()`. If false, fall back to pessimistic.
3. Same for internal page merges in `RemoveKeyValueInInternalPage`.

### BUG 6 (APPLIED): OptimisticTraverseNode — keep parent read-locked

**Change:** In `OptimisticTraverseNode`, removed `read_set.pop_front()` at the leaf case. The parent's read lock is now held until the optimistic operation completes (ctx goes out of scope). This prevents concurrent splits/merges on the leaf.

### BUG 7 (APPLIED): Header read lock held for entire optimistic operation

**Change:** In `InsertOptimistic` and `RemoveOptimistic`, moved `header_guard` out of the scoped block so it stays alive for the entire function. This prevents concurrent pessimistic operations from modifying the tree structure (root splits, etc.) during the optimistic path.

---

## ACTIVE BUG: Separator Key Corruption (MixTest1/MixTest2)

### Symptom
After interleaved inserts and deletes, internal page separator keys don't match their children's actual first keys. This causes `TraverseNodesToLeaf` to route keys to the wrong leaf page, resulting in silent delete failures (key not found in wrong leaf).

### Evidence
```
TRAVERSAL BUG: key not in leaf range. key=1, leaf_range=[2, 4], leaf_size=3
TRAVERSAL BUG: key not in leaf range. key=5, leaf_range=[6, 8], leaf_size=3
TRAVERSAL BUG: key not in leaf range. key=9, leaf_range=[6, 8], leaf_size=2
```

Pattern: odd keys (1, 5, 9, 13, ...) consistently route to the leaf containing their EVEN successors ([2,4], [6,8], etc.). The separator key between the leaf that should contain the odd key and the leaf that does contain it is WRONG — it's too small, routing the odd key rightward past its correct leaf.

### Test Details (MixTest1)
- `leaf_max_size=3, internal_max_size=5, BPM=50 frames`
- Pre-inserts 500 odd keys [1, 3, 5, ..., 999]
- Then 10 threads (5 insert even keys [2,4,...,1000], 5 delete odd keys [1,3,...,999])
- Each of the 5 insert threads inserts ALL 500 even keys (duplicates return false)
- Each of the 5 delete threads deletes ALL 500 odd keys (already-deleted return silently)
- Runs 20 iterations with fresh tree each time
- Template: `MixTest1Call<0>()` (NumTombs=0) then `MixTest1Call<3>()` (NumTombs=3)

### Key Findings
- **Bug persists even with optimistic paths completely disabled** (pessimistic only)
- Since pessimistic operations hold the header write lock (only one at a time), operations are **fully serialized** — this is a **logic bug, NOT a concurrency race**
- All single-threaded tests pass (insert, delete, sequential edge mix)
- The `ValidateInternalPage()` assertions (duplicate child pointer check) do NOT fire — child pointers are correct, only separator KEYS are wrong
- Bug manifests specifically with interleaved insert+delete pattern on small page sizes

### What Has Been Ruled Out
1. **Concurrency race** — bug persists with pessimistic-only (fully serialized) operations
2. **Duplicate child pointers** — ValidateInternalPage catches these; none fire after ShiftKeyAndValueRight(0) fix
3. **Iterator bugs** — insert/delete tests all pass; iterator constructor deadlock was a separate bug (fixed)
4. **Optimistic path issues** — bug exists without optimistic paths
5. **Tombstone-specific** — bug appears with NumTombs=0 (no tombstones at all)

### Suspected Root Causes (not yet confirmed)
The separator key in a parent becomes stale when a child's first key changes. This can happen in several places:

1. **After leaf redistribute from LEFT sibling** (`Remove` line ~598-600):
   ```cpp
   parent_page->SetKeyAt(child_index, leaf_page->KeyAt(0));
   ```
   The separator is set to the FIRST key of the current page after redistribute. If `RedistributeLeafPageLeftSibling` doesn't correctly update `KeyAt(0)`, the separator is wrong.

2. **After leaf redistribute from RIGHT sibling** (`Remove` line ~569-570):
   ```cpp
   parent_page->SetKeyAt(child_index + 1, right_sibling_page->KeyAt(0));
   ```
   The separator is set to the first key of the right sibling. Should be correct if the right sibling's first entry is updated properly.

3. **After leaf merge + cascade** — `RemoveKeyValueInInternalPage` calls `ShiftKeyAndValueLeft(child_index)` which removes the separator. The ADJACENT separator (shifted into the removed position) must correctly separate the remaining children. This is inherently correct IF the tree was consistent before the merge.

4. **After insert split** — `SplitLeafPage` returns `pushed_up_key = old_page->KeyAt(mid)`. `InsertToParent` inserts this as the separator. If `mid` is computed wrong or the split distributes entries incorrectly, the separator could be wrong.

5. **Interaction: delete causes redistribute/merge that changes a leaf's first key, then a SUBSEQUENT insert/delete traverses using the stale separator.** Since operations are serialized (pessimistic), the separator should be updated before the next operation starts. Unless the separator update is SKIPPED or WRONG.

### How to Debug (for next session)
1. **Write a standalone single-threaded test** that reproduces the pattern:
   - Insert odd keys 1-999 into tree with leaf_max_size=3, internal_max_size=5
   - Shuffle and interleave: insert even keys + delete odd keys
   - After each operation, call `ValidateTreeHelper` to check separators
   - This avoids the lock contention issue from calling validation in concurrent mode

2. **Add fprintf to separator update points** — log every `SetKeyAt` call on internal pages with the old and new separator values:
   - `Remove` line ~600: after left redistribute
   - `Remove` line ~570: after right redistribute
   - `InsertToParent` line ~331: after insert into parent
   - `RemoveKeyValueInInternalPage` line ~817: after shift left

3. **Check `RedistributeLeafPageLeftSibling` return value and separator update** — the function returns true/false. If it returns true but the separator update at line 600 is wrong (e.g., `leaf_page->KeyAt(0)` is a tombstoned key or the borrowed entry is at the wrong position), the separator would be stale.

4. **Validate the `ValidateTreeHelper` approach** — the function calls `ReadPage` which acquires read locks. With pessimistic-only mode and validation called AFTER all locks are released (at the end of Insert/Remove), it should work. BUT it ran into issues previously:
   - The concurrent test kept running stale binary (build succeeded but test wasn't re-run)
   - When it did run, it hung — likely because the validation is called from within Insert/Remove while the header write lock or other locks are still held
   - **Fix:** Move the `ValidateTreeHelper` call to AFTER ctx is fully destroyed and all guards released. Or better: write a separate single-threaded test.

### Temporary Debug Code in Source (to be cleaned up)
- `b_plus_tree.cpp`: `ValidateInternalPage()` static function + calls after every internal page modification
- `b_plus_tree.cpp`: `ValidateTreeHelper()` template function + calls after Insert/Remove (may cause hangs in concurrent mode — move to single-threaded test)
- `b_plus_tree.cpp`: Traversal bug logging in `Remove()` key-not-found path
- `b_plus_tree.cpp`: Optimistic paths temporarily disabled (commented out in `Insert` and `Remove`)
- `b_plus_tree.cpp`: Various `BUSTUB_ENSURE` assertions for child index validation
- All debug code MUST be removed before submission

---

## Page Guard Semantics (from investigation)

| Property | Behavior |
|----------|----------|
| Multiple read latches simultaneously | YES (via `std::shared_mutex::lock_shared()`) |
| Write latch blocks reads | YES (exclusive semantics) |
| Move semantics | Ownership transfer, latch NOT re-acquired |
| Destructor releases latch | YES (calls `Drop()`) |
| `.Drop()` on invalid guard | No-op (guarded by `is_valid_` check) |
| Default constructor | `is_valid_ = false`, all shared_ptrs null |
| `GetPageId()` on invalid guard | CRASHES (assertion failure) |
| Read-to-write upgrade | NO (must drop then re-acquire) |
| Reentrant (same thread, same page) | **NO — will deadlock** |

---

## Bugs Fixed (All Sessions)

| Bug | Fix | Location |
|-----|-----|----------|
| Root size==1 not promoted | `header_page->root_page_id_ = page->ValueAt(0)` | `b_plus_tree.cpp` |
| RemoveIndexFromTombstones off-by-one | Loop bound `i < num_tombstones_ - 1` | `leaf_page.cpp` |
| GetMinSize inconsistent (floor vs ceil) | Removed internal override, base uses `ceil()` | `b_plus_tree_page.cpp` |
| Init() missing leaf_max_size_ | `leaf_page->Init(leaf_max_size_)` | `b_plus_tree.cpp` |
| NumTombs=0 crash on delete | `if constexpr` branch for physical deletion | `b_plus_tree.cpp` |
| ShiftKeyAndValueLeft wrong index | Uses `child_index` not `child_index + 1` | `b_plus_tree.cpp` |
| MergeTwoInternalPages missing SetSize | `dest_page->SetSize(n + src_page->GetSize())` | `b_plus_tree.cpp` |
| Leaf merge not wired to parent cascade | Calls `RemoveKeyValueInInternalPage` after merge | `b_plus_tree.cpp` |
| Merge overfull (size > max_size) | Evict tombstones after merge until `size <= max_size` | `b_plus_tree.cpp` |
| Begin() erroneous `pop_front()` | Removed — header guard was never in `read_set_` | `b_plus_tree.cpp` |
| Iterator end `key_index_` mismatch | Reset `key_index_ = 0` in `LoadPageAndIterator` when `INVALID_PAGE_ID` | `index_iterator.cpp` |
| Buffer pool exhaustion on insert | `.Drop()` guards before recursing | `b_plus_tree.cpp` |
| Buffer pool exhaustion on delete | `.Drop()`/`.reset()` guards before recursing | `b_plus_tree.cpp` |
| TraverseNodesToLeaf leaf safe check wrong for delete | Use logical size (physical - tombstones) | `b_plus_tree.h` |
| **Iterator deadlock (double read-latch)** | Constructor accepts `ReadPageGuard&&` directly | `index_iterator.h/cpp`, `b_plus_tree.cpp` |
| **RedistributeInternalLeft duplicate child ptrs** | `ShiftKeyAndValueRight(0)` not `(1)` | `b_plus_tree.cpp` |
| **Leaf redistribute tombstone overflow** | Evict tombstones before `ShiftKeyAndValueRight` | `b_plus_tree.cpp` |
| **Dead page write by optimistic path** | Mark dead pages INVALID + check in optimistic paths | `b_plus_tree.cpp` |
| **OptimisticTraverseNode parent released too early** | Keep parent read-locked until op completes | `b_plus_tree.cpp` |
| **Header released too early in optimistic** | Keep header read-locked for entire optimistic op | `b_plus_tree.cpp` |

---

## Remaining TODOs

1. **Fix separator key corruption bug** (MixTest1/MixTest2) — see "ACTIVE BUG" section above
2. Re-enable optimistic paths after fixing core logic bug
3. Remove all debug validation code before submission
4. Run all tests: `b_plus_tree_insert_test`, `b_plus_tree_delete_test`, `b_plus_tree_iterator_test`, `b_plus_tree_concurrent_test`
5. Run `make format`, `make check-lint`, `make check-clang-tidy-p2` before submission

---

## Key Design Decisions & Patterns

### Internal Page Key/Value Layout
- `key[0]` is INVALID (not used as separator)
- `key[i]` (i >= 1) separates `ValueAt(i-1)` and `ValueAt(i)`
- Binary search: `left = 1, right = size - 1, while (left <= right)` → result at `right` (index of rightmost key <= search_key)
- `ValueAt(right)` is the child to follow

### Redistribute from LEFT sibling (internal page)
Correct sequence with `ShiftKeyAndValueRight(0)`:
```
1. ShiftKeyAndValueRight(0) — shift ALL entries right (key[0] is garbage anyway)
2. SetKeyAt(1, parent_separator) — old separator becomes key[1]
3. SetValueAt(0, sibling_rightmost_ptr) — borrowed pointer at leftmost position
4. parent.SetKeyAt(child_index, sibling_rightmost_key) — push sibling's key up
```

### Tombstone Handling During Redistribute
- Before shifting right on curr_page, evict tombstones if physically full
- Transfer tombstones along with borrowed keys
- Update tombstone indices after shifts (IncrementAll/DecreaseAll)

### Iterator Constructor (new signature)
```cpp
IndexIterator(shared_ptr<TracedBufferPoolManager> bpm, const KeyComparator &comparator,
              ReadPageGuard leaf_guard, page_id_t page_id, const optional<KeyType> &key);
```
- Begin(): `{bpm_, comparator_, std::move(ctx.read_set_.back()), page_id, key}`
- End(): `{bpm_, comparator_, ReadPageGuard{}, INVALID_PAGE_ID, nullopt}`

---

## C++ Template Gotchas

### Template Keyword for Dependent Types
```cpp
auto *page = guard.template As<LeafPage>();    // With auto guard
auto *page = guard.As<LeafPage>();             // With explicit ReadPageGuard guard
```

### if constexpr for NumTombs=0
```cpp
if constexpr (LEAF_PAGE_TOMB_CNT == 0) {
  // Physical deletion — no tombstone operations
} else {
  // Tombstone path
}
```

### C++ Standard: C++17
No C++20 features (concepts, etc.)

---

## Testing

```bash
cd build
make b_plus_tree_insert_test -j$(sysctl -n hw.ncpu) && ./test/b_plus_tree_insert_test
make b_plus_tree_delete_test -j$(sysctl -n hw.ncpu) && ./test/b_plus_tree_delete_test
make b_plus_tree_concurrent_test -j$(sysctl -n hw.ncpu) && ./test/b_plus_tree_concurrent_test
```

### Debugging with lldb
```bash
./test/b_plus_tree_concurrent_test --gtest_filter="BPlusTreeConcurrentTest.MixTest1" &
TEST_PID=$!
sleep 15
lldb -b -o "process attach --pid $TEST_PID" -o "thread backtrace all" -o "detach" -o "quit"
kill -9 $TEST_PID
```

### Tree Visualization
```bash
make b_plus_tree_printer -j$(sysctl -n hw.ncpu)
./bin/b_plus_tree_printer
> 2 3       # leaf_max_size=2, internal_max_size=3
> i 1       # insert key 1
> d 1       # delete key 1
> g tree.dot  # generate dot file (NOT 'f'!)
> q
```

## Formatting (required for grade)

```bash
make format
make check-lint
make check-clang-tidy-p2
```
