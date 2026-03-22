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

### OptimisticDeleteTest — ✅ DONE (Task #4 complete)
- Test expects read latches during traversal (optimistic latch crabbing)
- `RemoveOptimistic()` uses read-latch traversal + write-latch only on leaf

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
| **Iterator deadlock on construction** | Constructor now takes `ReadPageGuard leaf_guard` (moved from caller) instead of calling `bpm_->ReadPage()` internally, which would double read-latch the same page | `index_iterator.cpp:38-75` |
| **LoadPageAndIterator stack overflow** | Changed from recursive to iterative `while (true)` loop when following sibling chain of fully-tombstoned pages | `index_iterator.cpp:89-127` |

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
              ReadPageGuard leaf_guard, page_id_t page_id, const optional<KeyType>& key);
```
- Takes a `ReadPageGuard` directly from the caller (moved in) — avoids double read-latch deadlock
- No default constructor — end sentinel is constructed with `ReadPageGuard{}` + `INVALID_PAGE_ID`
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
| `IndexIterator(bpm, comparator, leaf_guard, page_id, key)` | ✅ DONE |
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

### LoadPageAndIterator — Key logic (iterative, not recursive)
Uses `while (true)` loop instead of recursion to follow sibling chains:
1. If `page_id == INVALID_PAGE_ID` → reset `key_index_ = 0` and return (end sentinel)
2. Read page via `bpm_->ReadPage(page_id)`, get leaf pointer
3. If `key` has value → binary search (lower_bound: first index where `key_at(i) >= key`)
4. Build tombstone index set
5. `FindAndSetValidIndex()` — skip tombstoned indices forward
6. If `key_index_ < size` → return (found valid entry)
7. Otherwise → set `page_id = next_page_id` and loop (all entries tombstoned or past end)

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

**Begin()** (`b_plus_tree.cpp:966`):
- Empty tree → return end sentinel `{bpm_, comparator_, ReadPageGuard{}, INVALID_PAGE_ID, nullopt}`
- Traverse to leftmost leaf: always follow `ValueAt(0)` at each internal node, pop parent after pushing child
- Passes `std::move(ctx.read_set_.back())` as `leaf_guard` to iterator constructor (avoids double-latch)

**Begin(key)** (`b_plus_tree.cpp:1002`):
- Empty tree → return end sentinel
- Uses `TraverseNodesToLeaf(ctx.read_set_, key, true, true)` to find leaf containing key
- Passes `std::move(ctx.read_set_.back())` as `leaf_guard` and `key` to iterator constructor

**End()** (`b_plus_tree.cpp:1028`): Returns `{bpm_, comparator_, ReadPageGuard{}, INVALID_PAGE_ID, nullopt}`

### Key Decisions
- **Tombstone skipping by index**: Uses `unordered_set<size_t>` of tombstoned indices (from `GetIndexesInTombstones()`), not keys — avoids `GenericKey` hash/comparator issues
- **FindAndSetValidIndex()**: Skips consecutive tombstoned indices starting from current `key_index_`
- **LoadPageAndIterator()**: Iteratively follows `next_page_id_` via `while (true)` loop if all entries on a page are tombstoned
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

## BUG (FIXED): Iterator Constructor Deadlock — Double Read-Latch

### Root Cause
`Begin()` and `Begin(key)` in `BPlusTree` held a `ReadPageGuard` on the leaf page (from traversal), then passed just the `page_id` to the `IndexIterator` constructor. The constructor called `bpm_->ReadPage(page_id)` internally, attempting to acquire a **second** read latch on the same page in the same thread → deadlock (BusTub's read latches are not reentrant).

### Fix
Changed the iterator constructor signature to accept a `ReadPageGuard leaf_guard` parameter (moved in):
```cpp
IndexIterator(shared_ptr<TracedBufferPoolManager> bpm, const KeyComparator& comparator,
              ReadPageGuard leaf_guard, page_id_t page_id, const optional<KeyType>& key);
```
- `Begin()` / `Begin(key)` now `std::move()` the read guard from `ctx.read_set_.back()` directly into the iterator
- `End()` passes an empty `ReadPageGuard{}` (no page to latch for end sentinel)
- `LoadPageAndIterator()` still calls `bpm_->ReadPage()` internally — this is safe because it's loading a **different** page (the next sibling)

### Key Insight
The caller already holds the read latch from traversal. Moving it into the iterator (ownership transfer) avoids re-acquiring it. This is the standard RAII pattern: transfer ownership, don't duplicate.

---

## BUG (FIXED): LoadPageAndIterator Stack Overflow — Recursive to Iterative

### Root Cause
`LoadPageAndIterator` was recursive: when all entries on a page were tombstoned, it called itself with `next_page_id`. A long chain of fully-tombstoned pages could overflow the stack.

### Fix
Converted to iterative `while (true)` loop at `index_iterator.cpp:90-127`. The loop advances `page_id = leaf_page_->GetNextPageId()` and re-enters from the top, breaking out when a valid entry is found or `INVALID_PAGE_ID` is reached.

---

## BUG (FIXED): Header Write Lock Released Early in Remove — Concurrent Access

### Root Cause
In `RemoveKeyValueInInternalPage`, when the root is promoted (root has size 1 after a merge), the code moved the header guard out of `ctx.header_page_`:
```cpp
auto header_page_guard = std::move(ctx.header_page_.value());  // MOVES OUT
// ... update header ...
// header_page_guard goes out of scope → HEADER WRITE LOCK RELEASED EARLY
```
This released the header write lock while the Remove operation was still in progress, allowing other threads to enter the pessimistic path concurrently.

Same issue in `Remove()` when root leaf becomes empty:
```cpp
auto header_guard2 = std::move(ctx.header_page_.value());  // MOVES OUT → EARLY RELEASE
```

### Fix
Access the header page through `ctx.header_page_` without moving the guard out:
```cpp
auto header_page = ctx.header_page_.value().AsMut<BPlusTreeHeaderPage>();
header_page->root_page_id_ = page->ValueAt(0);  // or INVALID_PAGE_ID
```
The guard stays in `ctx.header_page_` and is released when `ctx` goes out of scope at the end of `Remove()`.

Also removed `ctx.header_page_ = std::nullopt` and `DrainQueueUntilSize(ctx.write_set_, 0)` from Insert's split path — let `ctx` destructor handle cleanup to avoid early header release.

### Verification
With both optimistic paths disabled, MixTest1 passes **20/20** runs after this fix (was failing 100% before).

---

## BUG (IN PROGRESS): Optimistic Insert Concurrency Issue — MixTest1/MixTest2

### Status: Root cause NOT yet identified

### What We Know
1. **Pessimistic path is correct**: With both `InsertOptimistic` and `RemoveOptimistic` disabled (returning nullopt/false), MixTest1 passes 20/20.
2. **InsertOptimistic is the culprit**: Disabling only InsertOptimistic fixes MixTest1. Disabling only RemoveOptimistic does NOT fix it.
3. **Single-threaded logic is correct**: A single-threaded test with the same operations (including repeated insert/delete of same keys) passes 100%.
4. **TSAN detects data races**: On `FrameHeader::is_write_` and `FrameHeader::is_dirty_` fields. These are in the buffer pool code (Project 1). The `is_write_` race is between `CheckedWritePage` (sets under `bpm_latch_`) and `WritePageGuard::Drop()` (sets under `rwlatch_`). The `is_dirty_` race is via `GetDataMut()`.
5. **Holding all parent read latches doesn't help**: Even when `OptimisticTraverseNode` keeps ALL parent read latches (no `pop_front`), the test still fails.
6. **Global mutex on Remove serializes everything and passes**: A `std::mutex` on Remove alone fixes MixTest1.

### Hypotheses to Investigate
1. **Buffer pool data race causes corruption**: The TSAN-detected races on `is_dirty_`/`is_write_` might cause the page data to be corrupted or stale reads. Fixing these races in the BPM might fix the B+ tree test.
2. **Read→Write gap in OptimisticTraverseNode**: Between dropping the leaf's read latch and acquiring the write latch, a pessimistic operation could restructure the tree (despite holding parent read latch). The parent read latch should prevent this, but maybe there's an edge case.
3. **Stale root_page_id**: InsertOptimistic reads root_page_id from header, releases header read latch, then a pessimistic Remove changes the root (promotion). InsertOptimistic is now traversing from a stale root. The stale root still leads to valid pages, but the traversal path might be incorrect.

### Key Experimental Results
| Configuration | MixTest1 Result |
|---|---|
| Both optimistic enabled | FAIL 100% |
| Both optimistic disabled + header fix | PASS 20/20 |
| InsertOptimistic only (RemoveOpt disabled) | FAIL 100% |
| RemoveOptimistic only (InsertOpt disabled) | PASS 5/5 |
| Global mutex on Remove only | PASS 3/3 |
| Global mutex on Insert only | FAIL |
| Global mutex on both | PASS 3/3 |
| All safe-node checks disabled (pessimistic only) | Still FAILS |

### Test Parameters (MixTest1)
- `leaf_max_size=3`, `internal_max_size=5`, `BPM_SIZE=50`
- 1000 keys total: even (insert), odd (delete)
- 10 threads: 5 insert, 5 delete, each processes ALL their keys
- Runs 20 iterations per call; calls `MixTest1Call<0>()` then `MixTest1Call<3>()`

---

## Remaining TODOs

1. **Fix InsertOptimistic concurrency bug** (MixTest1/MixTest2)
2. **Task #3**: Test iterator (`make b_plus_tree_iterator_test`)
3. **Task #4**: Finish concurrent tests (`make b_plus_tree_concurrent_test`)
4. Run `make format`, `make check-lint`, `make check-clang-tidy-p2` before submission
