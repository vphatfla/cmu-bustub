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
| Task #3 | Index Iterator | 🔄 IN PROGRESS |
| Task #4 | Concurrency Control (latch crabbing) | 🔄 IN PROGRESS |

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

## Key Design Decisions & Fixes (Session 2026-02-21)

### GetMinSize — Uses `ceil(max_size / 2)` everywhere
- `BPlusTreePage::GetMinSize()` (base class) uses `ceil()` — both leaf and internal inherit this
- Internal page override was **removed** — single consistent formula
- `b_plus_tree_page.cpp:44-46`

### LeafPage::Init() must pass `leaf_max_size_`
- `b_plus_tree.cpp:150`: `leaf_page->Init(leaf_max_size_)` — NOT the default `Init()`
- Without this, leaf pages get `max_size = LEAF_PAGE_SLOT_CNT` (~509) instead of user-specified size

### NumTombs = 0 Handling — Physical deletion
- When `LEAF_PAGE_TOMB_CNT == 0` (default for most instantiations), tombstone buffer is zero-length
- `IsTombstonesFull()` returns `num_tombstones_ >= 0` → **always true** → crashes if tombstone ops called
- **Fix:** Use `if constexpr (LEAF_PAGE_TOMB_CNT == 0)` to branch to physical deletion (shift left + decrease size)
- This is done ONCE before the size check, avoiding code duplication

### Remove() Flow — Unified delete-then-check pattern
```
Remove(key):
1. Tree empty → return
2. Find leaf, find key's index (binary search)
3. Key not found → return
4. Key already tombstoned → return

5. Perform deletion (single path):
   - If LEAF_PAGE_TOMB_CNT == 0: physical delete (ShiftLeft + SetSize)
   - Else if tombstone buffer has space: AddIndexToTombstones
   - Else: DeleteOldestKeyInTombstones, re-find key, AddIndexToTombstones

6. Check resulting logical_size = GetSize() - GetTombstonesSize():
   - If >= min_size → return (page is fine)
   - If root page → allow underfull, check if empty, return
   - If < min_size → redistribute or merge, cascade up
```

### Merge overfull fix — ✅ DONE
- `MergeTwoLeafPages` copies ALL physical entries including tombstoned ones
- Combined physical size can exceed `max_size` due to tombstoned entries
- **Fixed** at `b_plus_tree.cpp:639-642`: after tombstone rebuild, evict until `size <= max_size`

### OptimisticDeleteTest — Expected failure (Task #4 not done)
- Test expects read latches during traversal (optimistic latch crabbing)
- Current Remove() uses WritePage for all traversal → 0 reads, N writes
- Will pass after Task #4 implementation

---

## Bugs Fixed This Session

| Bug | Fix | Location |
|-----|-----|----------|
| Root size==1 not promoted | `header_page->root_page_id_ = page->ValueAt(0)` | `b_plus_tree.cpp:658` |
| RemoveIndexFromTombstones off-by-one | Loop bound `i < num_tombstones_ - 1` | `leaf_page.cpp:111` |
| GetMinSize inconsistent (floor vs ceil) | Removed internal override, base uses `ceil()` | `b_plus_tree_page.cpp:44` |
| Init() missing leaf_max_size_ | `leaf_page->Init(leaf_max_size_)` | `b_plus_tree.cpp:150` |
| NumTombs=0 crash on delete | `if constexpr` branch for physical deletion | `b_plus_tree.cpp:411-421` |
| ShiftKeyAndValueLeft wrong index | Uses `child_index` not `child_index + 1` | `b_plus_tree.cpp:646` |
| MergeTwoInternalPages missing SetSize | `dest_page->SetSize(n + src_page->GetSize())` | `b_plus_tree.cpp:768` |
| Leaf merge not wired to parent cascade | Calls `RemoveKeyValueInInternalPage` after merge | `b_plus_tree.cpp:474-482` |
| Merge overfull (size > max_size) | Evict tombstones after merge until `size <= max_size` | `b_plus_tree.cpp:639-642` |
| Begin() erroneous `pop_front()` | Removed — header guard was never in `read_set_` | `b_plus_tree.cpp:804,840` |
| Iterator end `key_index_` mismatch | Reset `key_index_ = 0` in `LoadPageAndIterator` when `INVALID_PAGE_ID` | `index_iterator.cpp:59` |
| **Buffer pool exhaustion on insert** | `.Drop()` guards in `Insert()`/`InsertToParent()` before recursing | `b_plus_tree.cpp:191,324` |
| **Buffer pool exhaustion on delete** | `.Drop()`/`.reset()` guards in `Remove()`/`RemoveKeyValueInInternalPage()` before recursing | `b_plus_tree.cpp:565,840` |
| **TraverseNodesToLeaf leaf safe check wrong for delete** | Leaf safe check for delete must use logical size (physical - tombstones), not physical size | `b_plus_tree.h:224-233` |

---

## Task #1: B+Tree Pages - DONE

### Base Page (BPlusTreePage)
**Header: 12 bytes**
| Field | Size | Description |
|-------|------|-------------|
| page_type_ | 4 | INVALID_INDEX_PAGE / LEAF_PAGE / INTERNAL_PAGE |
| size_ | 4 | Number of key/value pairs |
| max_size_ | 4 | Max key/value pairs |

### Internal Page
- Stores **m keys** and **m+1 child pointers** (page_ids)
- **key[0] is INVALID** - lookups start from index 1
- `GetMinSize()` inherited from base: `ceil(max_size / 2)`

### Leaf Page
**Header: 16 bytes** (base 12 + next_page_id 4)
- Stores **m keys** and **m values** (RIDs)
- `next_page_id_` for sibling traversal (iterator)
- `GetMinSize()` inherited from base: `ceil(max_size / 2)`
- **Tombstone buffer**: lazy deletion
  - `num_tombstones_` tracks count, `tombstones_[LEAF_PAGE_TOMB_CNT]` stores indexes (FIFO)
  - `LEAF_PAGE_TOMB_CNT = ((NumTombs < 0) ? 0 : NumTombs)` — compile-time constant
  - When NumTombs=0: no buffer, physical deletion only

---

## Task #2: B+Tree Operations - DONE

### Implementation Status
| Method | Status |
|--------|--------|
| `GetValue()` | ✅ DONE |
| `IsEmpty()` | ✅ DONE |
| `GetRootPageId()` | ✅ DONE |
| `Insert()` | ✅ DONE |
| `Remove()` | ✅ DONE (cascading to parent works) |

### Insert Flow
1. Acquire write guard on header page
2. If tree empty: create new leaf as root with `Init(leaf_max_size_)`
3. Traverse to leaf, push guards to write_set_
4. If room: `InsertKVToLeafPage` (handles duplicates, tombstone reuse)
5. If full: `SplitLeafPage` with tombstone distribution, insert into correct half
6. `InsertToParent()` propagates up recursively

### Remove Flow (unified delete-then-check)
1. Find leaf, find key index
2. Perform deletion: `if constexpr (LEAF_PAGE_TOMB_CNT == 0)` → physical, else → tombstone
3. Check `logical_size = GetSize() - GetTombstonesSize()`
4. If `>= min_size` → done
5. If root → allow underfull (set empty if `GetSize() == GetTombstonesSize()`)
6. Try redistribute left, then right
7. If neither works → merge with sibling → `RemoveKeyValueInInternalPage` cascades up

### RemoveKeyValueInInternalPage — Cascading delete
1. `ShiftKeyAndValueLeft(child_index)` + decrease size
2. If `size >= min_size` → done
3. If root with `size == 1` → promote only child as new root
4. If not root and underfull → try redistribute left/right, else merge + recurse

---

## Internal Page Operations

### Redistribute from LEFT sibling
Keys rotate through parent: sibling → parent → current (3-way rotation)
```
curr.ShiftKeyAndValueRight(1)
curr.SetKeyAt(1, parent.KeyAt(child_index))        // pull separator DOWN
curr.SetValueAt(0, sibling.ValueAt(n-1))            // borrow rightmost ptr
parent.SetKeyAt(child_index, sibling.KeyAt(n-1))    // push key UP
```

### Redistribute from RIGHT sibling
```
curr.SetKeyAt(n, parent.KeyAt(child_index+1))         // pull separator DOWN
parent.SetKeyAt(child_index+1, sibling.KeyAt(1))       // push key UP
curr.SetValueAt(n, sibling.ValueAt(0))                  // borrow leftmost ptr
sibling.SetValueAt(0, sibling.ValueAt(1))               // promote next ptr
sibling.ShiftKeyAndValueLeft(1)                          // shift rest left
```
**Gotcha:** Must `SetValueAt(0, ValueAt(1))` BEFORE `ShiftKeyAndValueLeft(1)`

### MergeTwoInternalPages
- Separator key from parent is PULLED DOWN into merged node
- No tombstones on internal pages
- `dest_page->SetSize(n + src_page->GetSize())`

---

## Tombstone Spec

### Key Rules
- Tombstones MUST be maintained across ALL operations (split/merge/redistribute)
- When buffer full: apply ONLY the OLDEST tombstone (FIFO eviction)
- k = `LEAF_PAGE_TOMB_CNT` = compile-time buffer capacity from `NumTombs` template param
- When k=0: no tombstone buffer, physical deletion only

### Template Instantiations
```cpp
BPlusTree<GenericKey<8>, RID, GenericComparator<8>>       // NumTombs=0 (default)
BPlusTree<GenericKey<8>, RID, GenericComparator<8>, 3>    // NumTombs=3
BPlusTree<GenericKey<8>, RID, GenericComparator<8>, 2>    // NumTombs=2
BPlusTree<GenericKey<8>, RID, GenericComparator<8>, 1>    // NumTombs=1
BPlusTree<GenericKey<8>, RID, GenericComparator<8>, -1>   // NumTombs=-1 → TOMB_CNT=0
```

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
make b_plus_tree_insert_test -j$(sysctl -n hw.ncpu)
./test/b_plus_tree_insert_test
make b_plus_tree_delete_test -j$(sysctl -n hw.ncpu)
./test/b_plus_tree_delete_test
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
Visualize: `dot -Tpng -O tree.dot` or paste into http://dreampuf.github.io/GraphvizOnline/

**Printer NumTombs:** Hardcoded in `tools/b_plus_tree_printer/b_plus_tree_printer.cpp:82`. Change the template parameter and rebuild to test different tombstone sizes.

---

## Formatting (required for grade)

```bash
make format
make check-lint
make check-clang-tidy-p2
```

---

## Task #3: Index Iterator - IN PROGRESS

### Iterator Constructor (current signature)
```cpp
IndexIterator(shared_ptr<TracedBufferPoolManager> bpm, const KeyComparator& comparator,
              page_id_t page_id, const optional<KeyType>& key);
```
- No default constructor — end sentinel is constructed with `INVALID_PAGE_ID`
- `key` param: if `nullopt`, starts at index 0; if provided, binary searches for lower_bound position
- Uses `TracedBufferPoolManager` (not plain `BufferPoolManager`)

### Iterator Members
- `bpm_`: `shared_ptr<TracedBufferPoolManager>`
- `comparator_`: `KeyComparator` (marked `[[maybe_unused]]`)
- `read_guard_`: `ReadPageGuard` of current leaf page
- `leaf_page_`: `const LeafPage*` pointer into the guard
- `page_id_`: current page id (`INVALID_PAGE_ID` = end)
- `key_index_`: current index within leaf page
- `tombstone_indices_set_`: `unordered_set<size_t>` for O(1) tombstone lookup

### Implementation Status
| Method | Status |
|--------|--------|
| `IndexIterator(bpm, comparator, page_id, key)` | ✅ DONE |
| `~IndexIterator()` | ✅ DONE (default) |
| `IsEnd()` | ✅ DONE |
| `operator*()` | ✅ DONE |
| `operator++()` | ✅ DONE |
| `operator==` / `operator!=` | ✅ DONE |
| `FindAndSetValidIndex()` | ✅ DONE |
| `LoadPageAndIterator(page_id, key)` | ✅ DONE |
| `Begin()` in BPlusTree | ✅ DONE |
| `Begin(key)` in BPlusTree | ✅ DONE |
| `End()` in BPlusTree | ✅ DONE |

### LoadPageAndIterator — Key logic
1. If `page_id == INVALID_PAGE_ID` → reset `key_index_ = 0` and return (end sentinel)
2. Read page, get leaf pointer
3. If `key` has value → binary search (lower_bound: first index where `key_at(i) >= key`)
4. Build tombstone index set
5. `FindAndSetValidIndex()` — skip tombstoned indices forward
6. If `key_index_ >= size` → recurse to `next_page_id` (all entries tombstoned or past end)

### Binary Search in LoadPageAndIterator — Lower bound pattern
```cpp
auto left = 0, right = leaf_page_->GetSize();
while (left < right) {
    auto mid = left + (right - left) / 2;
    if (comparator_(leaf_page_->KeyAt(mid), key.value()) < 0) {
        left = mid + 1;   // key_at(mid) < target → mid is too small, exclude
    } else {
        right = mid;       // key_at(mid) >= target → mid could be answer, keep
    }
}
key_index_ = left;  // converged: first index where key_at(index) >= target
```
- `left = mid + 1`: safe because `key_at(mid) < target` means mid cannot be the answer
- `right = mid` (NOT `mid - 1`): `key_at(mid) >= target` means mid *could* be the answer
- `cmp == 0` is handled by the `else` branch (same as `cmp > 0`)

### Begin() / Begin(key) / End() in BPlusTree

**Begin()** (`b_plus_tree.cpp:789`):
- Empty tree → return end sentinel
- Traverse to leftmost leaf: always follow `ValueAt(0)` at each internal node, pop parent after pushing child

**Begin(key)** (`b_plus_tree.cpp:825`):
- Empty tree → return end sentinel
- Uses `TraverseNodesToLeaf(ctx.read_set_, key, true)` to find leaf containing key
- Passes `key` to iterator constructor for lower_bound positioning

**End()** (`b_plus_tree.cpp:852`): Correct — returns `{bpm_, comparator_, INVALID_PAGE_ID, nullopt}`

### Key Decisions
- **Tombstone skipping by index**: Uses `unordered_set<size_t>` of tombstoned indices (from `GetIndexesInTombstones()`), not keys — avoids `GenericKey` hash/comparator issues
- **FindAndSetValidIndex()**: Skips consecutive tombstoned indices starting from current `key_index_`
- **LoadPageAndIterator()**: Recursively follows `next_page_id_` if all entries on a page are tombstoned
- **operator==**: Compares `page_id_` and `key_index_` (position equality, not object identity)
- **operator!=**: Simply `!(*this == itr)`
- **Begin(key) with tombstoned key**: Iterator automatically skips to next valid entry (lower_bound on logical keys)
- **operator++** passes `std::nullopt` to `LoadPageAndIterator` when jumping to next page (always start from index 0)

---

### Gotcha: `operator==` vs `IsEnd()` consistency
- `operator==` compares both `page_id_` AND `key_index_`
- `IsEnd()` only checks `page_id_ == INVALID_PAGE_ID`
- When reaching end (via `operator++` → `LoadPageAndIterator(INVALID_PAGE_ID)`), `key_index_` must be reset to 0
- Otherwise `operator!=` with `End()` returns true (stale `key_index_` != 0), but `IsEnd()` returns true → assertion in `operator*()`

---

## Task #4: Concurrency Control - IN PROGRESS

### Strategy: Optimistic Latch Crabbing
1. **Optimistic path** (fast): Read-latch header → root → internals (crabbing). Write-latch only the leaf.
   - If leaf operation is "safe" (insert: not full, delete: won't go underfull) → do it, done.
   - If unsafe → release everything, fall back to pessimistic path.
2. **Pessimistic path** (existing code): Write-latch from header all the way down to leaf.

### Implementation Status
| Component | Status |
|-----------|--------|
| `InsertOptimistic()` | ✅ DONE |
| `OptimisticTraverseNode()` | ✅ DONE |
| `RemoveOptimistic()` | ✅ DONE |
| `GetValue()` latch crabbing | ✅ DONE (read-latch crabbing with `release_parent=true`) |
| `Begin()` / `Begin(key)` latch crabbing | ✅ DONE (read-latch crabbing) |

### InsertOptimistic Flow (`b_plus_tree.cpp:199`)
1. Read-latch header, check empty tree → `nullopt` (pessimistic creates root)
2. Read-latch root, check if root is leaf → `nullopt` (pessimistic handles single-leaf root)
3. `OptimisticTraverseNode()`: read-latch down internals, write-latch the leaf
4. Check `size < max_size` → insert and return `true`
5. Otherwise → `nullopt` (triggers pessimistic split path)

### OptimisticTraverseNode (`b_plus_tree.cpp:230`)
- Binary search internal page (same as `TraverseNodesToLeaf`)
- Read-latch child: if internal → push onto read_set, pop parent, recurse
- If child is leaf: drop read guard on leaf, write-latch leaf, then drop parent read guard
- **Gap safety**: Parent read latch is held during the read→write gap on the leaf, preventing structural changes (splits/merges require parent write latch)

### Latch Ordering Rules (from spec)
- Never acquire same read latch twice in a single thread (deadlock risk)
- Release latches in order: header → root → internals → leaf (top-down)
- No global latches allowed

### Key Fixes
| Bug | Fix |
|-----|-----|
| Empty tree returns `false` instead of `nullopt` | Changed to `return std::nullopt` so pessimistic path creates root |
| Root-is-leaf not handled | Added `IsLeafPage()` check, returns `nullopt` |
| Binary search `left < right` | Fixed to `left <= right` to match `TraverseNodesToLeaf` |

### Test Expectations
- **OptimisticInsertTest**: `reads > 0, writes == 1` (read traversal + 1 leaf write)
- **OptimisticDeleteTest**: `reads > 0, writes == 1` (same pattern)

---

## BUG (FIXED): Buffer Pool Exhaustion — Pin Accumulation During Cascading Splits/Merges

### Root Cause
During cascading splits (insert) or merges (delete), recursive calls in `InsertToParent` / `RemoveKeyValueInInternalPage` kept page guards alive on the call stack. Each recursion level added ~2 pinned frames that couldn't be evicted, eventually exhausting the 30-frame buffer pool → `std::abort()`.

### Fix: Early `.Drop()` Before Recursion
- **`Insert()`**: Drop `leaf_guard` and `new_leaf_guard` before calling `InsertToParent()`
- **`InsertToParent()`**: Drop `parent_guard` and `new_page_guard` before recursive call
- **`Remove()`**: Drop `leaf_guard` and unused sibling guard (`.reset()`) before `RemoveKeyValueInInternalPage`
- **`RemoveKeyValueInInternalPage()`**: Drop `guard` and unused sibling guard before recursive call
- Safe because: pages are in final state after split/merge, and the grandparent write latch prevents other threads from reaching them

---

## BUG (FIXED): TraverseNodesToLeaf Leaf Safe Check Wrong for Delete (Tombstones)

### Root Cause
`TraverseNodesToLeaf` applied the same safe-node check to ALL pages including leaves. For delete, the check was `GetSize() > GetMinSize()` using **physical size**. With tombstones, physical size != logical size. A leaf with physical size 3 but 2 tombstones (logical size 1) would pass the safe check and release all ancestors. After deletion, logical size dropped to 0 (underfull), but ancestors were already released → tree corruption (leaf treated as root).

### Fix
In `TraverseNodesToLeaf` (`b_plus_tree.h`), handle leaf page separately:
- **Insert**: safe check uses physical size (correct — tombstones don't affect insert capacity)
- **Delete**: safe check uses **logical size** (`GetSize() - GetTombstonesSize() > GetMinSize()`)
- Internal nodes: unchanged (no tombstones, physical = logical)

---

## Remaining TODOs

1. **Task #3**: Test iterator (`make b_plus_tree_iterator_test`)
2. **Task #4**: Run concurrent tests (`make b_plus_tree_concurrent_test`)
3. Run `make format`, `make check-lint`, `make check-clang-tidy-p2` before submission
