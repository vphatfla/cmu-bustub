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

---

## Task #1: B+Tree Pages - DONE ✓

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
- Layout:
  ```
  key_array_:     [INVALID] [key1] [key2] ... [key_n-1]
  page_id_array_: [ptr0]    [ptr1] [ptr2] ... [ptr_n-1]
  ```

### Leaf Page
**Header: 16 bytes** (base 12 + next_page_id 4)
- Stores **m keys** and **m values** (RIDs)
- `next_page_id_` for sibling traversal (iterator)
- **Tombstone buffer**: lazy deletion (Bε-tree concept)
  - `num_tombstones_` tracks count, `tombstones_[]` stores indexes (FIFO-like)
  - Deletion appends index to buffer instead of removing immediately
  - **When buffer full (k entries): ONLY the OLDEST deletion is applied** (not all!)
  - Tombstones MUST be maintained across split/merge/redistribute operations
  - `KeyAt()` returns physical entry regardless of tombstone
  - `GetTombstones()` returns keys with pending deletes

**Why Tombstones Are Efficient:**
- **Key insight: k << n** (buffer size k is MUCH smaller than array size n)
- Typical values: k=1-5, n=50-200
- Without tombstones: Every deletion costs O(n) shifts
- With tombstones: Amortized cost O(n/k) per deletion (k times faster!)
- Iterating over k tombstones is negligible compared to shifting n entries
- Example: k=3, n=100 → 3 deletions cost 106 ops vs 300 ops = 3x speedup
- Critical for B+ trees: Reduces expensive disk I/O operations

---

## Task #2: B+Tree Operations - TODO

### Required Methods
- `Insert(key, value)` → bool (false if duplicate key)
- `Remove(key)`
- `GetValue(key, result)` → bool
- `GetRootPageId()` → page_id_t

### Key Rules

**Insertion:**
- Only unique keys (return false for duplicates)
- Split leaf when size reaches `max_size` AFTER insertion
- Split internal when size reaches `max_size` BEFORE insertion
- Update `root_page_id` in header page if root changes

**Deletion:**
- Merge/redistribute when page less than half full
- **Tombstones MUST be maintained across merge/split/redistribute**
- When buffer full during normal deletion: apply ONLY the oldest tombstone, then add new deletion
- **NEVER** compact all tombstones - this violates the spec!

**Tombstone Handling Rules:**
- Normal deletion with space: add index to tombstone buffer
- Normal deletion when buffer full: apply oldest tombstone, then add new deletion
- Split operation: distribute tombstones to both split pages (maintain them!)
- Merge operation: copy both pages' tombstones to merged page (maintain them!)
- Redistribute: move tombstone with borrowed entry if applicable

**Header Page:**
- Access via `header_page_id_` (given in constructor)
- `reinterpret_cast` to `BPlusTreeHeaderPage`
- Stores `root_page_id`

### Context Class (optional helper)
```cpp
class Context {
  std::optional<WritePageGuard> header_page_;  // Lock on header
  page_id_t root_page_id_;                     // Current root
  std::deque<WritePageGuard> write_set_;       // Path of pages
  std::deque<ReadPageGuard> read_set_;         // Read-locked pages
};
```
Tips:
- `write_set_.back()` = parent of current node
- Set `header_page_ = std::nullopt` to unlock header
- Pop from `write_set_` and drop to unlock pages

---

## Task #3: Index Iterator - TODO

### Required Methods
- `isEnd()` → bool
- `operator++()` → move to next key/value
- `operator*()` → return current key/value pair
- `operator==()` / `operator!=()` → compare iterators

### BPlusTree Methods
- `Begin()` → iterator to first entry
- `End()` → end iterator

**Important:** Iterator must skip tombstoned entries!

---

## Task #4: Concurrency Control - TODO

- Use **optimistic latch crabbing** technique
- Use `ReadPageGuard` and `WritePageGuard` from Project #1
- Release parent latches as soon as safe
- **Never acquire same read latch twice** (deadlock risk)
- Release latches in same order as acquired (header → bottom)
- No global latch allowed

---

## Development Roadmap

1. **Simple Inserts** - Insert KV into non-full node
2. **Simple Search** - Find key in tree
3. **Simple Splits** - Split full leaf on insert
4. **Multiple Splits** - Handle cascading splits up the tree
5. **Simple Deletes** - Delete from half-full+ leaf
6. **Simple Coalesces** - Merge underfull nodes
7. **Complex Coalesces** - Redistribute when can't merge
8. **Index Iterators** - Task #3
9. **Concurrent Indexes** - Task #4

---

## Important Hints

- **Use binary search** for key lookups (otherwise timeout)
- **Use buffer pool** for new nodes (no malloc/new for large blocks)
- **Page guards** recommended for thread safety
- `BUSTUB_ASSERT` for debug assertions (not in release)
- `BUSTUB_ENSURE` for assertions in all modes

---

## Common Pitfalls

- Don't test iterator for thread-safe scans
- Release latches in acquisition order (header → bottom)
- Only add trivially-constructed types to page classes (no vectors)
- Don't modify `key_array_` and `value_array_` structure

---

## C++ Template Gotchas

### Template Keyword for Dependent Types
When using `auto` with template methods inside the BPlusTree class, you need the `template` keyword:
```cpp
// ERROR: "use template keyword to treat As as dependent name"
auto guard = bpm_->ReadPage(page_id);
auto *page = guard.As<LeafPage>();  // Won't compile!

// SOLUTION 1: Use template keyword
auto guard = bpm_->ReadPage(page_id);
auto *page = guard.template As<LeafPage>();  // Works

// SOLUTION 2: Explicit type declaration (no template keyword needed)
ReadPageGuard guard = bpm_->ReadPage(page_id);
auto *page = guard.As<LeafPage>();  // Works
```

### PageGuard Methods
| Method | Guard Type | Returns | Use For |
|--------|-----------|---------|---------|
| `As<T>()` | Both | `const T*` | Read-only access |
| `AsMut<T>()` | WritePageGuard only | `T*` | Mutable access |

### Comparator Usage
```cpp
// comparator_(a, b) returns:
//   < 0  if a < b
//   == 0 if a == b
//   > 0  if a > b
int cmp = comparator_(key, other_key);
```

### C++ Standard: C++17
Project uses `CMAKE_CXX_STANDARD 17`. C++20 features like `concepts` are NOT available.

**Type constraints (C++17 style):**
```cpp
#include <type_traits>

template<typename Guard>
auto SomeFunction(Guard& guard) -> int {
  static_assert(std::is_same_v<Guard, ReadPageGuard> ||
                std::is_same_v<Guard, WritePageGuard>,
                "Guard must be ReadPageGuard or WritePageGuard");
  // ...
}
```

---

## Testing

```bash
cd build
make b_plus_tree_insert_test -j$(nproc)
./test/b_plus_tree_insert_test

# Other tests:
make b_plus_tree_sequential_scale_test
make b_plus_tree_delete_test
make b_plus_tree_concurrent_test
```

### Tree Visualization
```bash
make b_plus_tree_printer -j
./bin/b_plus_tree_printer
>> 5 5       # set leaf/internal max_size
>> f input.txt  # insert from file
>> g tree.dot   # output dot file
>> q
```
Then: `dot -Tpng -O tree.dot` or use http://dreampuf.github.io/GraphvizOnline/

---

## Formatting (required for grade)

```bash
make format
make check-lint
make check-clang-tidy-p2
```

---

## Template Macros

```cpp
// For internal page / B+ tree (3 params)
#define INDEX_TEMPLATE_ARGUMENTS \
  template <typename KeyType, typename ValueType, typename KeyComparator>

// For leaf page (4 params, includes tombstone count)
#define FULL_INDEX_TEMPLATE_ARGUMENTS_DEFN \
  template <typename KeyType, typename ValueType, typename KeyComparator, ssize_t NumTombs = 0>
```

| Parameter | Purpose |
|-----------|---------|
| KeyType | Key type (e.g., GenericKey<8>) |
| ValueType | page_id_t for internal, RID for leaf |
| KeyComparator | Comparison functor |
| NumTombs | Tombstone buffer size (leaf only, default 0) |

---

## Implementation Status

### Task #1: Pages - ✅ DONE
- **BPlusTreePage**: `IsLeafPage()`, `GetSize()`, `SetSize()`, `GetMaxSize()`, `SetMaxSize()`, `GetMinSize()`
- **InternalPage**: `Init()`, `KeyAt()`, `SetKeyAt()`, `ValueAt()`, `SetValueAt()`, `ValueIndex()`, `ShiftKeyAndValueRight()`
- **LeafPage**: `Init()`, `KeyAt()`, `SetKeyAt()`, `ValueAt()`, `SetValueAt()`, `GetTombstones()`, `GetNextPageId()`, `SetNextPageId()`, `IsIndexInTombstones()`, `RemoveIndexFromTombstones()`, `ShiftKeyAndValueRight()`, `ShiftKeyAndValueLeft()`, `CompactTombstones()`

**Tombstone handling - ⚠️ NEEDS UPDATE:**
```cpp
IsIndexInTombstones(index)         // Check if index is in tombstone buffer
RemoveIndexFromTombstones(index)   // Remove index from tombstone buffer
AddIndexToTombstones(index)        // Add index to tombstone buffer
GetIndexesInTombstones()           // Get all tombstone indices
ApplyOldestTombstone()             // ❌ TODO: Apply ONLY oldest tombstone when buffer full
ClearTombstones()                  // ❌ TODO: Clear tombstone buffer (for utility only)
CompactTombstones()                // ❌ DEPRECATED: Should NEVER be called per spec!
```
**CRITICAL:** Never call `CompactTombstones()` - tombstones must be maintained across all operations!

### Task #2: Operations - IN PROGRESS
- `GetValue()` - ✅ DONE
- `IsEmpty()` - ✅ DONE
- `GetRootPageId()` - ✅ DONE
- `Insert()` - ⚠️ NEEDS FIX (remove CompactTombstones call, fix SplitLeafPage to distribute tombstones)
- `Remove()` - 🔄 IN PROGRESS (basic logic done, needs ApplyOldestTombstone + merge/redistribute)

**Helper methods added to BPlusTree:**
```cpp
auto FindInsertPositionInLeafPage(LeafPage* page, const KeyType& key) const -> size_t;  // ✅ DONE
auto FindInsertPositionInInternalPage(InternalPage* page, const KeyType& key) const -> size_t;  // ✅ DONE
auto InsertKVToLeafPage(LeafPage* page, const KeyType& key, const ValueType& value) -> bool;  // ✅ DONE
auto InsertKVToInternalPage(InternalPage* page, const KeyType& key, page_id_t page_id) -> bool;  // ✅ DONE
void InsertToParent(const KeyType& key, page_id_t page_id, Context& ctx);  // ✅ DONE
auto SplitLeafPage(LeafPage* old_page) -> std::tuple<KeyType, WritePageGuard, page_id_t>;  // ✅ DONE
auto SplitInternalPage(InternalPage* old_page) -> std::tuple<KeyType, WritePageGuard, page_id_t>;  // ✅ DONE
auto CreateNewRootAndUpdateHeader(Context& ctx) -> std::pair<WritePageGuard, page_id_t>;  // ✅ DONE

// Template function for traversal (in header file)
template <typename GuardType>
void TraverseNodesToLeaf(std::deque<GuardType>& guard_set, const KeyType& key, bool release_parent);  // ✅ DONE
```

**TraverseNodesToLeaf Usage:**
- Caller must push root guard onto `guard_set` before calling
- Uses `if constexpr` to handle ReadPageGuard vs WritePageGuard at compile time
- `release_parent=true`: releases parent guards as we go down (for read operations)
- `release_parent=false`: keeps all guards for potential splits (for write operations)

**Insert Flow - ⚠️ NEEDS FIX (remove compact step):**
1. Fetch header, create root if empty OR fetch existing root
2. Traverse internal pages to find leaf (push guards to write_set_)
3. At leaf: check if space available
4. If room: `InsertKVToLeafPage` (handles duplicates, tombstone reuse)
5. If full: `SplitLeafPage` with tombstone distribution, insert into correct half
6. `InsertToParent()` propagates key up recursively:
   - If write_set_ empty (was root): `CreateNewRootAndUpdateHeader()`
   - If parent has room: `InsertKVToInternalPage()`
   - If parent full: `SplitInternalPage()` and recurse

**Split Functions - ⚠️ NEEDS FIX (distribute tombstones):**
- `SplitLeafPage`: Splits at `ceil(size/2)`, updates sibling pointers, **distributes tombstones**, returns pushed-up key
  - Tombstones with index < mid stay in old page
  - Tombstones with index >= mid move to new page (adjust index by subtracting mid)
- `SplitInternalPage`: Splits at `ceil(size/2)`, returns push-up key (key at mid is NOT copied to new page)
- Both return tuple: (pushed_up_key, new_page_guard, new_page_id)

**Helper methods added to LeafPage:**
```cpp
void SetKeyAt(int index, const KeyType &key);                          // ✅ DONE
auto ValueAt(int index) const -> ValueType;                            // ✅ DONE
void SetValueAt(int index, const ValueType &value);                    // ✅ DONE
auto IsIndexInTombstones(const size_t index) const -> bool;            // ✅ DONE
void RemoveIndexFromTombstones(const size_t index);                    // ✅ DONE
void AddIndexToTombstones(const size_t index);                         // ✅ DONE
auto GetIndexesInTombstones() const -> std::vector<size_t>;            // ✅ DONE
void ShiftKeyAndValueRight(const size_t index);                        // ✅ DONE
void ShiftKeyAndValueLeft(const size_t index);                         // ✅ DONE
void ApplyOldestTombstone();                                           // ❌ TODO
void ClearTombstones();                                                // ❌ TODO
void CompactTombstones();  // ⚠️ DEPRECATED - DO NOT USE!
```

**Helper methods added to InternalPage (all ✅ DONE):**
```cpp
void SetValueAt(const size_t index, const ValueType &value);
void ShiftKeyAndValueRight(const size_t index);
```

### Task #3: Iterator - TODO
- `IndexIterator` class, `Begin()`, `End()`

### Task #4: Concurrency - TODO
- Latch crabbing implementation

---

## Memory Model

Pages use **embedded arrays** (not std::vector):
```cpp
KeyType key_array_[SLOT_CNT];
ValueType value_array_[SLOT_CNT];
```

**Why:** Pages accessed via `reinterpret_cast` from buffer pool. No constructors run.

**Usage:**
```cpp
auto guard = bpm->WritePage(page_id);
auto *page = guard.AsMut<InternalPage>();
// use page->KeyAt(), etc.
// guard releases automatically
```

---

## BPlusTree Class Structure

### Member Variables (b_plus_tree.h:131-137)
| Variable | Type | Description |
|----------|------|-------------|
| `bpm_` | `shared_ptr<TracedBufferPoolManager>` | Buffer pool manager |
| `index_name_` | `string` | Name of the index |
| `comparator_` | `KeyComparator` | Key comparison functor |
| `leaf_max_size_` | `int` | Max entries in leaf node |
| `internal_max_size_` | `int` | Max entries in internal node |
| `header_page_id_` | `page_id_t` | Page ID of header page (NOT root!) |

### Header Page vs Root Page
```
header_page_id_ (fixed, passed to constructor)
       │
       ▼
┌─────────────────────────┐
│   BPlusTreeHeaderPage   │
│  ┌───────────────────┐  │
│  │  root_page_id_    │──┼──► Actual root of B+ tree (changes during splits)
│  └───────────────────┘  │
└─────────────────────────┘
```

- `header_page_id_` is **fixed** - known at construction, never changes
- `root_page_id_` is **dynamic** - stored IN the header page, updated on root splits/merges

### Constructor Initialization (b_plus_tree.cpp:20-31)
```cpp
BPlusTree::BPlusTree(..., page_id_t header_page_id, ...) {
  WritePageGuard guard = bpm_->WritePage(header_page_id_);
  auto root_page = guard.AsMut<BPlusTreeHeaderPage>();
  root_page->root_page_id_ = INVALID_PAGE_ID;  // Empty tree - no root yet
}
```
Sets `root_page_id_ = INVALID_PAGE_ID` to indicate an empty tree.

### How to Access Root Page ID
```cpp
// Read the current root from header page
auto header_guard = bpm_->ReadPage(header_page_id_);
auto *header = header_guard.As<BPlusTreeHeaderPage>();
page_id_t root_id = header->root_page_id_;

// Check if tree is empty
if (root_id == INVALID_PAGE_ID) {
  // Tree is empty, need to create first leaf as root
}
```

### Methods to Implement (Task #2)
| Method | Status | Notes |
|--------|--------|-------|
| `IsEmpty()` | ✅ DONE | Check if `root_page_id_ == INVALID_PAGE_ID` |
| `GetValue(key, result)` | ✅ DONE | Point query - traverse to leaf, use binary search |
| `Insert(key, value)` | ✅ DONE | Insert with splits and propagation |
| `Remove(key)` | TODO | Delete with merge/redistribute |
| `GetRootPageId()` | ✅ DONE | Read from header page |

### GetValue() Implementation Pattern
```cpp
// 1. Get header page, check if empty
ReadPageGuard header_guard = bpm_->ReadPage(header_page_id_);
auto *header = header_guard.template As<BPlusTreeHeaderPage>();
if (header->root_page_id_ == INVALID_PAGE_ID) return false;

// 2. Fetch root and check type
ReadPageGuard root_guard = bpm_->ReadPage(header->root_page_id_);
auto *page = root_guard.template As<BPlusTreePage>();

if (page->IsLeafPage()) {
  auto *leaf = root_guard.template As<LeafPage>();
  // Binary search for key in leaf
} else {
  auto *internal = root_guard.template As<InternalPage>();
  // Traverse down to leaf
}
```

---

## Insert() Implementation Notes - ✅ COMPLETE

### Implemented Flow (⚠️ NEEDS FIX - step 7)
1. Acquire write guard on header page
2. If tree empty: create new leaf as root, init it, push guard to write_set_
3. Traverse internal pages (binary search), push guards to write_set_
4. At leaf: find insert position via binary search
5. Check for duplicate (handle tombstone reuse)
6. If space available: shift right and insert
7. If no space: split page while **maintaining tombstones** (distribute to both pages)
8. After split: insert into correct half, `InsertToParent()` propagates up

### Split Functions (✅ DONE)
```cpp
// Leaf split: returns pushed_up_key + guard + page_id, updates sibling pointers
auto SplitLeafPage(LeafPage* old_page) -> std::tuple<KeyType, WritePageGuard, page_id_t>;

// Internal split: returns push-up key + guard + page_id
auto SplitInternalPage(InternalPage* old_page) -> std::tuple<KeyType, WritePageGuard, page_id_t>;
```
- Both split at `ceil(size/2)` - left keeps first `mid` entries
- After split, caller determines which half gets the new key (compare with pushed-up key)
- Caller inserts into appropriate half, then calls `InsertToParent()` to propagate

### Binary Search for Insert Position (lower_bound style)
```cpp
size_t left = 0, right = page->GetSize();
while (left < right) {
    auto mid = left + (right - left) / 2;
    if (comparator_(page->KeyAt(mid), key) < 0) {
        left = mid + 1;
    } else {
        right = mid;
    }
}
return left;  // First index where key_array[i] >= key
```

### ShiftKeyAndValueRight - Correct Loop
```cpp
// Shift elements [index, size-1] to [index+1, size]
for (auto i = GetSize(); i > static_cast<int>(index); i--) {
    key_array_[i] = key_array_[i-1];
    rid_array_[i] = rid_array_[i-1];
}
// Note: i > index, NOT i >= index
```

### RemoveIndexFromTombstones - Correct Loop
```cpp
// Shift elements left to remove tombstone at pos
for (size_t i = pos; i < num_tombstones_ - 1; i++) {
    tombstones_[i] = tombstones_[i+1];
}
num_tombstones_--;
// Note: i < num_tombstones_ - 1, NOT i < num_tombstones_
```

### Insert() Implementation - ⚠️ NEEDS FIX
Insert-related functions status:
- Tree traversal with write guards - ✅ DONE
- `InsertKVToLeafPage()` - handles duplicates, tombstone reuse - ✅ DONE
- `InsertKVToInternalPage()` - shifts and inserts to internal node - ✅ DONE
- `SplitLeafPage()` - ⚠️ NEEDS FIX: must distribute tombstones to both pages
- `SplitInternalPage()` - splits at ceil(size/2) - ✅ DONE
- `InsertToParent()` - recursive propagation up the tree - ✅ DONE
- `CreateNewRootAndUpdateHeader()` - creates new root when splitting root node - ✅ DONE
- `DrainQueueUntilSize()` - utility to release page guards - ✅ DONE

**BUG:** Current Insert() calls `CompactTombstones()` before split - this violates spec!

### Remove() Implementation - TODO

**Key Concepts:**
- `logical_size = size_ - num_tombstones_` (actual valid entries)
- `min_size = ceil(max_size / 2)` for leaves
- Tombstone buffer size = `NumTombs` template parameter (compile-time, default 0)

**Remove() Flow (CORRECTED):**
```
Remove(key):
1. Tree empty → return
2. Find leaf, find key's index (binary search)
3. Key not found → return
4. Key already tombstoned → return (already deleted)
5. new_logical_size = (size_ - num_tombstones_) - 1

6. If new_logical_size < min_size:
   → Go directly to redistribute/merge
   → Mark deletion during that process
   → Maintain tombstones across the operation

7. If new_logical_size >= min_size (page stays valid):
   - If tombstone buffer has space → add index to buffer, return
   - If tombstone buffer full → ApplyOldestTombstone(), then add new index to buffer
```

**Why check logical_size FIRST:**
- Avoids unnecessary operations when redistribute/merge is inevitable
- Redistribute/merge will reorganize data anyway
- Can handle deletion as part of that process

**Buffer Full Handling:**
- When buffer full: apply ONLY the oldest tombstone (FIFO eviction)
- Then add the new deletion index to buffer
- **NEVER** compact all tombstones!

**Redistribute vs Merge decision:**
- Try redistribute first (borrow from sibling)
- If sibling also at min_size → must merge/coalesce
- **Maintain tombstones from both pages** during merge/redistribute

**Merge/Redistribute Tombstone Handling:**
- Merge: copy ALL entries (including tombstoned) + copy tombstone buffers with adjusted indices
- Redistribute: move tombstone with borrowed entry if applicable
- **NEVER compact tombstones** during these operations!

**Propagation to parent:**
- After merge: remove key from parent (may trigger cascading merges)
- Update parent's child pointers accordingly
- If root becomes empty after merge → update header to new root

---

## 🔴 CRITICAL: Tombstone Spec Clarification (Session 2026-02-08)

### Key Findings:

**WRONG UNDERSTANDING (previous):**
- ❌ Compact all tombstones before split to avoid unnecessary splits
- ❌ Compact recipient's tombstones before merge
- ❌ When tombstone buffer full, compact all tombstones

**CORRECT UNDERSTANDING (per spec):**
- ✅ Tombstones MUST be maintained across ALL operations (split/merge/redistribute)
- ✅ When buffer full: apply ONLY the OLDEST tombstone (FIFO eviction)
- ✅ **NEVER** compact all tombstones during normal operations
- ✅ Split: distribute tombstones to both pages
- ✅ Merge: copy both pages' tombstones to merged page

### Spec Quote:
> "only when the buffer of said leaf page has k entries in it is the **oldest** buffered deletion actually applied"
> "leaf page tombstones (and their ordering) **must be maintained** across any merging, splitting, and redistributing operations"

### Why This Design is Efficient:

**Core principle: k << n (buffer size much smaller than array size)**

```
Tombstone buffer size (k): 1-5 entries (small)
Key/value array size (n):  50-200 entries (large)

Cost analysis:
- Without tombstones: Every delete = O(n) shifts
- With tombstones: Amortized = O(n/k) per delete (k times faster!)
- Iterating k tombstones is negligible vs shifting n entries
```

**Why iteration on tombstones is OK:**
- When k=3, adjusting tombstones = ~6 operations
- Shifting n=100 entries = ~100 operations
- Trade 6 ops for 100 ops = massive win!
- Additionally reduces disk I/O (critical for B+ trees)

### Required Fixes:

1. **Insert()**: Remove `CompactTombstones()` call before split
2. **SplitLeafPage()**: Add tombstone distribution logic
3. **LeafPage**: Implement `ApplyOldestTombstone()` method
4. **Remove()**: Use `ApplyOldestTombstone()` when buffer full (not CompactTombstones)
5. **Merge/Redistribute**: Preserve and copy tombstones from both pages

### Method Status:
- `CompactTombstones()` - ⚠️ **DEPRECATED**: Should never be called!
- `ApplyOldestTombstone()` - ❌ **TODO**: Apply only oldest tombstone (FIFO eviction)
- `ClearTombstones()` - ❌ **TODO**: Utility to clear buffer (for split/merge operations)

---

## ApplyOldestTombstone() Implementation

**Critical method for maintaining tombstone FIFO behavior:**

```cpp
void ApplyOldestTombstone() {
  BUSTUB_ENSURE(num_tombstones_ > 0, "No tombstones to apply");

  // STEP 1: Get oldest tombstone (at index 0)
  auto oldest_index = tombstones_[0];

  // STEP 2: Physically delete that entry from key/value arrays
  ShiftKeyAndValueLeft(oldest_index);
  SetSize(GetSize() - 1);

  // STEP 3 & 4: Remove tombstones[0] and adjust remaining tombstone indices
  for (size_t i = 0; i < num_tombstones_ - 1; i++) {
    tombstones_[i] = tombstones_[i + 1];  // Shift tombstone buffer left

    // CRITICAL: Adjust indices - if a tombstone index > deleted index, decrement it
    if (tombstones_[i] > oldest_index) {
      tombstones_[i]--;
    }
  }
  num_tombstones_--;
}
```

**Why index adjustment is needed:**
- After deleting entry at `oldest_index`, all entries after it shift left
- Tombstone indices pointing to entries after the deleted one must be decremented
- Example: Delete index 2 → indices [0,1,2,3,4] become [0,1,3,4] → tombstone at old index 4 now at index 3

**Why this is efficient:**
- Loop runs k-1 times where k is buffer size (typically 1-3)
- When k=3: loop runs 2 times (~6 operations total)
- Compare to shifting n entries in key/value arrays (n=50-200)
- **Trade-off: 6 operations on tombstones vs 100+ operations on arrays**
- Since k << n, tombstone iteration is negligible

**Efficiency breakdown:**
```
Without tombstones (3 deletions):
  - Delete 1: Shift 100 entries
  - Delete 2: Shift 100 entries
  - Delete 3: Shift 100 entries
  Total: 300 operations

With tombstones (3 deletions):
  - Delete 1: Add to buffer (1 op)
  - Delete 2: Add to buffer (1 op)
  - Delete 3: Add to buffer (1 op)
  - Buffer full on Delete 4:
    * Iterate 3 tombstones (3 ops)
    * Shift 100 entries (100 ops)
  Total for 4 deletions: 106 operations

Amortized per deletion: 106/4 ≈ 26 ops vs 100 ops = 4x speedup!
```

---

## Split/Merge Tombstone Distribution

### SplitLeafPage Tombstone Handling:
```cpp
auto SplitLeafPage(LeafPage* old_page) {
  // ... create new page, split at mid ...

  // Distribute tombstones
  auto tombstones = old_page->GetIndexesInTombstones();
  old_page->ClearTombstones();  // Clear from old page

  for (auto tomb_idx : tombstones) {
    if (tomb_idx < mid) {
      // Stays in old page (same index)
      old_page->AddIndexToTombstones(tomb_idx);
    } else {
      // Moves to new page (adjust index)
      new_page->AddIndexToTombstones(tomb_idx - mid);
    }
  }

  // ... return tuple ...
}
```

### Merge Tombstone Handling:
```cpp
// Merge right sibling into current (recipient)
// Copy ALL physical entries
auto curr_size = leaf_page->GetSize();
for (int i = 0; i < sibling->GetSize(); i++) {
    leaf_page->SetKeyAt(curr_size + i, sibling->KeyAt(i));
    leaf_page->SetValueAt(curr_size + i, sibling->ValueAt(i));
}
leaf_page->SetSize(curr_size + sibling->GetSize());

// Copy donor's tombstones with adjusted indices
auto donor_tombstones = sibling->GetIndexesInTombstones();
for (auto tomb_idx : donor_tombstones) {
    leaf_page->AddIndexToTombstones(curr_size + tomb_idx);
}
```
