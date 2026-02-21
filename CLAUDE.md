# BusTub Working Context

> **CLAUDE CODE DIRECTIVE:** Automatically update this context file as you work. Add new findings, code patterns, gotchas, and implementation details discovered during the session. Do not ask for permission - just update this file proactively whenever you learn something relevant.

## Project: CMU 15-445 - Project 2: B+ Tree Index

**Due:** Oct 26, 2025 @ 11:59pm

---

## Tasks Overview

| Task | Description | Status |
|------|-------------|--------|
| Task #1 | B+Tree Pages (Base, Internal, Leaf) | ✅ DONE |
| Task #2 | B+Tree Operations (Insert, Delete, Search) | 🔄 IN PROGRESS |
| Task #3 | Index Iterator | TODO |
| Task #4 | Concurrency Control (latch crabbing) | TODO |

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

### Merge overfull issue (KNOWN, TODO)
- `MergeTwoLeafPages` copies ALL physical entries including tombstoned ones
- Combined physical size can exceed `max_size` (e.g., size=3, max_size=2)
- **Planned fix:** After merge tombstone rebuild, evict until `size <= max_size`:
  ```cpp
  while (dest_page->GetSize() > dest_page->GetMaxSize() && dest_page->GetTombstonesSize() > 0) {
    dest_page->DeleteOldestKeyInTombstones();
  }
  ```

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

## Task #2: B+Tree Operations - IN PROGRESS

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

## Remaining TODOs

1. **Merge overfull fix** — evict tombstones after merge until `size <= max_size`
2. **Task #3: Index Iterator** — `Begin()`, `End()`, `operator++`, skip tombstoned entries
3. **Task #4: Concurrency Control** — optimistic latch crabbing (read down, write only on leaf)
