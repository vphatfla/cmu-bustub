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
- **LeafPage**: `Init()`, `KeyAt()`, `SetKeyAt()`, `ValueAt()`, `SetValueAt()`, `GetTombstones()`, `GetNextPageId()`, `SetNextPageId()`, `IsIndexInTombstones()`, `RemoveIndexFromTombstones()`, `ShiftKeyAndValueRight()`, `ShiftKeyAndValueLeft()`

**Tombstone handling - ✅ IMPLEMENTED:**
```cpp
IsIndexInTombstones(index)           // ✅ Check if index is in tombstone buffer
RemoveIndexFromTombstones(index)     // ✅ Remove index from tombstone buffer
AddIndexToTombstones(index)          // ✅ Add index to tombstone buffer
GetIndexesInTombstones()             // ✅ Get all tombstone indices as vector
GetTombstones()                      // ✅ Get all tombstoned KEYS as vector
IsTombstonesFull()                   // ✅ Check if buffer is at capacity
GetTombstonesSize()                  // ✅ Get current tombstone count
ClearTombstones()                    // ✅ Reset tombstone buffer (for split operations)
DeleteOldestKeyInTombstones()        // ✅ FIFO eviction - apply oldest tombstone
IncrementAllTombstonesIndexes()      // ✅ For redistribute from left sibling
DecreaseAllTombstonesIndexes()       // ✅ For redistribute from right sibling
```

### Task #2: Operations - IN PROGRESS
- `GetValue()` - ✅ DONE
- `IsEmpty()` - ✅ DONE
- `GetRootPageId()` - ✅ DONE
- `Insert()` - ✅ DONE (with tombstone distribution on split)
- `Remove()` - 🔄 IN PROGRESS (leaf-level done, needs cascading to parent)

**Helper methods added to BPlusTree:**
```cpp
// Insert helpers - ✅ ALL DONE
auto FindInsertPositionInLeafPage(LeafPage* page, const KeyType& key) const -> size_t;
auto FindInsertPositionInInternalPage(InternalPage* page, const KeyType& key) const -> size_t;
auto InsertKVToLeafPage(LeafPage* page, const KeyType& key, const ValueType& value) -> bool;
auto InsertKVToInternalPage(InternalPage* page, const KeyType& key, page_id_t page_id) -> bool;
void InsertToParent(const KeyType& key, page_id_t page_id, Context& ctx);
auto SplitLeafPage(LeafPage* old_page) -> std::tuple<KeyType, WritePageGuard, page_id_t>;
auto SplitInternalPage(InternalPage* old_page) -> std::tuple<KeyType, WritePageGuard, page_id_t>;
auto CreateNewRootAndUpdateHeader(Context& ctx) -> std::pair<WritePageGuard, page_id_t>;

// Remove helpers - ✅ LEAF LEVEL DONE
auto FindIndexOfKeyInLeafPage(LeafPage* page, const KeyType& key) const -> std::optional<size_t>;
auto GetLeftSiblingPage(InternalPage* parent, int child_index) -> std::optional<WritePageGuard>;
auto GetRightSiblingPage(InternalPage* parent, int child_index) -> std::optional<WritePageGuard>;
auto RedistributeLeafPageLeftSibling(LeafPage* curr, LeafPage* sibling) -> bool;
auto RedistributeLeafPageRightSibling(LeafPage* curr, LeafPage* sibling) -> bool;
void MergeTwoLeafPages(LeafPage* dest_page, LeafPage* src_page);

// Internal page remove helpers
void RemoveKeyValueInInternalPage(Context& ctx, WritePageGuard guard, size_t child_index);
auto RedistributeInternalPageLeftSibling(InternalPage* curr, InternalPage* sibling, InternalPage* parent, int curr_child_index) -> bool;  // ✅ DONE
auto RedistributeInternalPageRightSibling(InternalPage* curr, InternalPage* sibling, InternalPage* parent, int curr_child_index) -> bool;  // ✅ DONE
// TODO: void MergeTwoInternalPages(...);

// Template function for traversal (in header file)
template <typename GuardType>
void TraverseNodesToLeaf(std::deque<GuardType>& guard_set, const KeyType& key, bool release_parent);
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

**Helper methods added to LeafPage (all ✅ DONE):**
```cpp
void SetKeyAt(int index, const KeyType &key);
auto ValueAt(int index) const -> ValueType;
void SetValueAt(int index, const ValueType &value);
auto IsIndexInTombstones(const size_t index) const -> bool;
void RemoveIndexFromTombstones(const size_t index);
void AddIndexToTombstones(const size_t index);
auto GetIndexesInTombstones() const -> std::vector<size_t>;
auto GetTombstones() const -> std::vector<KeyType>;  // Returns tombstoned KEYS
auto IsTombstonesFull() const -> bool;
auto GetTombstonesSize() const -> size_t;
auto GetMinSize() const -> size_t;  // ceil(max_size / 2)
void ShiftKeyAndValueRight(const size_t index);
void ShiftKeyAndValueLeft(const size_t index);
void DeleteOldestKeyInTombstones();  // FIFO eviction with index adjustment
void ClearTombstones();              // Reset buffer for split operations
void IncrementAllTombstonesIndexes();  // For redistribute from left
void DecreaseAllTombstonesIndexes();   // For redistribute from right
```

**Helper methods added to InternalPage (all ✅ DONE):**
```cpp
void SetValueAt(const size_t index, const ValueType &value);
void ShiftKeyAndValueRight(const size_t index);
auto ValueIndex(const ValueType &value) const -> int;  // Find child index
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

## Insert() Implementation - ✅ COMPLETE

### Implemented Flow
1. Acquire write guard on header page
2. If tree empty: create new leaf as root, init it, push guard to write_set_
3. Traverse internal pages (binary search), push guards to write_set_
4. At leaf: find insert position via binary search
5. Check for duplicate (handle tombstone reuse - if key exists but tombstoned, reuse slot)
6. If space available: shift right and insert
7. If no space: split page with **tombstone distribution** to both pages
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
- **SplitLeafPage distributes tombstones:** indices < mid stay, indices >= mid move (adjusted by -mid)

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

### Insert() Implementation - ✅ COMPLETE
All insert-related functions implemented:
- Tree traversal with write guards - ✅ DONE
- `InsertKVToLeafPage()` - handles duplicates, tombstone reuse - ✅ DONE
- `InsertKVToInternalPage()` - shifts and inserts to internal node - ✅ DONE
- `SplitLeafPage()` - splits at ceil(size/2), **distributes tombstones** - ✅ DONE
- `SplitInternalPage()` - splits at ceil(size/2) - ✅ DONE
- `InsertToParent()` - recursive propagation up the tree - ✅ DONE
- `CreateNewRootAndUpdateHeader()` - creates new root when splitting root node - ✅ DONE
- `DrainQueueUntilSize()` - utility to release page guards - ✅ DONE

### Remove() Implementation - 🔄 LEAF LEVEL DONE

**Key Concepts:**
- `logical_size = size_ - num_tombstones_` (actual valid entries)
- `min_size = ceil(max_size / 2)` for leaves
- Tombstone buffer size = `NumTombs` template parameter (compile-time, default 0)

**Remove() Flow - ✅ IMPLEMENTED:**
```
Remove(key):
1. Tree empty → return
2. Find leaf, find key's index (binary search)
3. Key not found → return
4. Key already tombstoned → return (already deleted)
5. new_logical_size = (size_ - num_tombstones_) - 1

6. If new_logical_size >= min_size (page stays valid):
   - If tombstone buffer has space → add index to buffer, return
   - If tombstone buffer full → DeleteOldestKeyInTombstones(), re-find key, add to buffer

7. If new_logical_size < min_size:
   - Add key to tombstone first
   - If root page (write_set empty) → can be underfull, just return
   - Try redistribute from left sibling → update parent separator key
   - Try redistribute from right sibling → update parent separator key
   - If neither works → merge with sibling
   - TODO: Cascade delete to parent after merge
```

**Root Leaf Page Handling (Session 2026-02-10):**
Per spec, tombstones should only be physically applied when buffer is full. When the root leaf page has all entries tombstoned (`size == num_tombstones`), we do NOT set tree to empty. Instead:
- Root pages can be underfull - standard B+ tree rule
- Keep tombstones as per spec (they're maintained, not discarded)
- Future inserts will reuse tombstoned slots
- Future deletes that fill buffer will trigger natural eviction

```cpp
if (ctx.write_set_.empty()) {
    // This is root page - can be underfull
    // Don't check for "logically empty" - just return and keep tombstones
    return;
}
```

**Edge case:** If `NumTombs = 0` (no tombstone buffer), deletions are immediate physical removal and `size` could reach 0. Only then should we consider setting tree to empty:
```cpp
if (ctx.write_set_.empty()) {
    if (leaf_page->GetSize() == 0) {
        // Truly empty (only possible when NumTombs = 0)
        auto header_guard = std::move(ctx.header_page_.value());
        auto header_page = header_guard.AsMut<BPlusTreeHeaderPage>();
        header_page->root_page_id_ = INVALID_PAGE_ID;
    }
    return;
}
```

**Redistribute Functions - ✅ IMPLEMENTED with defensive guards:**
```cpp
RedistributeLeafPageLeftSibling(curr_page, sibling_page):
  - Check sibling can spare (logical_size > min_size)
  - Loop while curr underfull AND sibling can spare:
    - Take rightmost entry from sibling
    - Insert at index 0 of curr (shift right first)
    - IncrementAllTombstonesIndexes() on curr
    - Transfer tombstone if borrowed entry was tombstoned
  - Return true if curr reached min_size

RedistributeLeafPageRightSibling(curr_page, sibling_page):
  - Check sibling can spare (logical_size > min_size)
  - Loop while curr underfull AND sibling can spare:
    - Take leftmost entry from sibling
    - Append to end of curr
    - ShiftKeyAndValueLeft(0) on sibling
    - DecreaseAllTombstonesIndexes() on sibling
    - Transfer tombstone if borrowed entry was tombstoned
  - Return true if curr reached min_size
```

**MergeTwoLeafPages - ✅ IMPLEMENTED with key-based tombstone rebuild:**
```cpp
MergeTwoLeafPages(dest_page, src_page):
  // src_page is merged INTO dest_page (src_page will be deleted by caller)
  1. Collect tombstoned KEYS from both pages (not indices)
  2. Clear dest's tombstones for fresh rebuild
  3. Copy all entries from src to dest
  4. Set combined size
  5. Rebuild tombstones by finding each key's current index:
     - Add dest's tombstones first (older, FIFO order)
     - Add src's tombstones (newer)
     - Evict if full → DeleteOldestKeyInTombstones adjusts indices
  6. Update sibling pointer (dest.next = src.next)
```
**Why key-based:** Avoids complex index offset tracking when evictions shift the array.

**TODO - Cascading to Parent:**
After leaf merge, must remove separator key from parent:
```
RemoveKeyFromInternalPage(parent, key_index):
├── Shift keys/values left to delete entry
├── Decrease size
├── If parent is root:
│   ├── Size >= 2 → Done (root can be underfull)
│   └── Size == 1 → Make only child the new root
└── If parent is not root:
    ├── Size >= min_size → Done
    └── Size < min_size → Redistribute or merge internal pages
        └── Recursively cascade up
```

**Internal Page Merge (different from leaf):**
- Separator key from parent is PULLED DOWN into merged node
- No tombstones on internal pages

---

## Tombstone Spec Clarification (Session 2026-02-08)

### Key Rules (per spec):
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

### Implementation Status - ✅ ALL DONE:
- `DeleteOldestKeyInTombstones()` - ✅ FIFO eviction with index adjustment
- `ClearTombstones()` - ✅ Reset buffer for split operations
- `IncrementAllTombstonesIndexes()` - ✅ For redistribute from left
- `DecreaseAllTombstonesIndexes()` - ✅ For redistribute from right
- `SplitLeafPage()` - ✅ Distributes tombstones to both pages
- `MergeTwoLeafPages()` - ✅ Key-based tombstone rebuild
- `RedistributeLeafPageLeftSibling()` - ✅ With tombstone transfer
- `RedistributeLeafPageRightSibling()` - ✅ With tombstone transfer

---

## DeleteOldestKeyInTombstones() Implementation - ✅ DONE

**Critical method for FIFO tombstone eviction (in b_plus_tree_leaf_page.cpp):**

```cpp
void DeleteOldestKeyInTombstones() {
  auto pos = tombstones_[0];  // Oldest tombstone index
  auto size = static_cast<size_t>(GetSize());

  // STEP 1: Physically delete entry at pos (shift array left)
  for (size_t i = pos; i < size - 1; i += 1) {
    key_array_[i] = key_array_[i + 1];
    rid_array_[i] = rid_array_[i + 1];
  }
  SetSize(size - 1);

  // STEP 2: Remove tombstones[0] and adjust remaining indices
  for (size_t i = 0; i < num_tombstones_ - 1; i += 1) {
    tombstones_[i] = tombstones_[i + 1];  // Shift buffer left
    if (tombstones_[i] > pos) {
      tombstones_[i] -= 1;  // Adjust for shifted array
    }
  }
  num_tombstones_ -= 1;
}
```

**Why index adjustment is needed:**
- After deleting entry at `pos`, all entries after it shift left
- Tombstone indices pointing to entries after the deleted one must be decremented
- Example: Delete index 2 → indices [0,1,2,3,4] become [0,1,3,4] → tombstone at old index 4 now at index 3

**Why this is efficient:**
- Loop runs k-1 times where k is buffer size (typically 1-3)
- When k=3: loop runs 2 times (~6 operations total)
- Compare to shifting n entries in key/value arrays (n=50-200)
- **Trade-off: 6 operations on tombstones vs 100+ operations on arrays**
- Since k << n, tombstone iteration is negligible

---

## Split/Merge/Redistribute Tombstone Handling - ✅ ALL IMPLEMENTED

### SplitLeafPage Tombstone Distribution (b_plus_tree.cpp:307-315):
```cpp
// Distribute tombstones to both pages based on which half they belong to
auto old_tombstones_indexes = old_page->GetIndexesInTombstones();
old_page->ClearTombstones();
for (const auto &i : old_tombstones_indexes) {
  if (i < mid) {
    old_page->AddIndexToTombstones(i);  // Stays in old page
  } else {
    new_leaf_page->AddIndexToTombstones(i - mid);  // Moves to new page (adjusted)
  }
}
```

### MergeTwoLeafPages - Key-Based Approach (b_plus_tree.cpp:599-642):
```cpp
// src_page is merged INTO dest_page
// Collect tombstoned KEYS (not indices) from both pages
auto dest_tomb_keys = dest_page->GetTombstones();
auto src_tomb_keys = src_page->GetTombstones();
dest_page->ClearTombstones();

// Copy all entries from src to dest
for (int i = 0; i < src_size; i++) {
  dest_page->SetKeyAt(dest_size + i, src_page->KeyAt(i));
  dest_page->SetValueAt(dest_size + i, src_page->ValueAt(i));
}
dest_page->SetSize(dest_size + src_size);

// Rebuild tombstones by finding each key's current index
for (const auto &key : dest_tomb_keys) {
  if (dest_page->IsTombstonesFull()) {
    dest_page->DeleteOldestKeyInTombstones();
  }
  auto idx = FindIndexOfKeyInLeafPage(dest_page, key);
  if (idx.has_value()) {
    dest_page->AddIndexToTombstones(idx.value());
  }
}
// Same for src_tomb_keys...
```
**Why key-based:** When evictions happen, they shift the array. Finding keys by value avoids tracking index offsets.

### RedistributeLeafPageLeftSibling Tombstone Handling:
```cpp
// Borrow from rightmost of left sibling, insert at index 0 of curr
curr_page->ShiftKeyAndValueRight(0);
curr_page->SetKeyAt(0, key);
curr_page->SetValueAt(0, value);
curr_page->IncrementAllTombstonesIndexes();  // All existing tombstones shift right

// If borrowed entry was tombstoned, transfer the tombstone
if (sibling_page->IsIndexInTombstones(sibling_index)) {
  if (curr_page->IsTombstonesFull()) {
    curr_page->DeleteOldestKeyInTombstones();
  }
  curr_page->AddIndexToTombstones(0);  // New entry is at index 0
  sibling_page->RemoveIndexFromTombstones(sibling_index);
}
```

### RedistributeLeafPageRightSibling Tombstone Handling:
```cpp
// Borrow from leftmost of right sibling, append to end of curr
curr_page->SetKeyAt(curr_index, key);
curr_page->SetValueAt(curr_index, value);

sibling_page->ShiftKeyAndValueLeft(0);
// If borrowed entry was tombstoned, transfer the tombstone
if (sibling_page->IsIndexInTombstones(0)) {
  if (curr_page->IsTombstonesFull()) {
    curr_page->DeleteOldestKeyInTombstones();
  }
  curr_page->AddIndexToTombstones(curr_index);
  sibling_page->RemoveIndexFromTombstones(0);
}
sibling_page->DecreaseAllTombstonesIndexes();  // All sibling tombstones shift left
```

---

## TODO: Internal Page Cascading Delete

After merging leaf pages, the separator key must be removed from parent:

```
Internal Page Merge (different from leaf merge):
├── Separator key from parent is PULLED DOWN into merged node
├── No tombstones on internal pages
└── May cascade further up the tree

RemoveKeyFromInternalPage(parent, key_index, ctx):
1. Shift keys/values left to remove entry at key_index
2. Decrease parent size
3. If parent is root:
   - Size >= 2 → Done
   - Size == 1 → Only child becomes new root, update header
4. If parent not root AND size < min_size:
   - Try redistribute from internal sibling
   - If can't redistribute → merge internal pages
   - Recursively RemoveKeyFromInternalPage on grandparent
```

**Internal Page Layout Reminder:**
```
key_array:     [ _ ] [K1] [K2] [K3]      (key[0] is INVALID)
page_id_array: [P0]  [P1] [P2] [P3]
                 │     │    │    │
              children with keys:
              <K1  <K2  <K3  >=K3
```

**Internal Redistribute — Borrow from LEFT sibling:**

Keys rotate through parent (sibling → parent → current), NOT directly between siblings.
This preserves the routing invariant since internal keys are separators, not data.

```
BEFORE:
              Parent
  keys:  [ _ ] [20] [50] [80]
  ptrs:  [L]   [C]  ...  ...
          │     │
          ▼     ▼
        Left          Current (underfull)
  [ _ ][5][10][15]     [ _ ][55]
  [A] [B] [C] [D]     [E]  [F]

Step 1: Pull separator (20) DOWN from parent → Current's new first key
Step 2: Move Left's rightmost pointer (D) → Current's position 0
Step 3: Push Left's rightmost key (15) UP to parent as new separator

AFTER:
              Parent
  keys:  [ _ ] [15] [50] [80]     ← 15 replaced 20
  ptrs:  [L]   [C]  ...  ...

        Left                 Current
  [ _ ] [5] [10]        [ _ ] [20] [55]
  [A]  [B]  [C]         [D]   [E]  [F]

Invariant check: Left keys < 15 ✓ | Current keys >= 15 ✓
Pointer D (keys 15..19) now routed under key 20 in Current ✓
```

**Internal Redistribute — Borrow from RIGHT sibling:**
```
BEFORE:
              Parent
  keys:  [ _ ] [20] [50] [80]
  ptrs:  ...   [C]  [R]  ...
                │    │
                ▼    ▼
          Current          Right
      (underfull)
       [ _ ][30]       [ _ ][60][70][75]
       [A]  [B]        [E]  [F] [G] [H]

Step 1: Pull separator (50) DOWN from parent → append to Current as last key
Step 2: Move Right's leftmost pointer (E) → Current's new last position
Step 3: Push Right's first valid key (60) UP to parent as new separator
        Right shifts its entries left to remove key 60 and pointer E

AFTER:
              Parent
  keys:  [ _ ] [20] [60] [80]     ← 60 replaced 50
  ptrs:  ...   [C]  [R]  ...

        Current                Right
  [ _ ] [30] [50]         [ _ ] [70] [75]
  [A]   [B]  [E]          [F]   [G]  [H]

Invariant check: Current keys < 60 ✓ | Right keys >= 60 ✓
Pointer E (keys 50..59) correctly under key 50 in Current ✓
```

**Why rotation through parent (not direct move)?**
Internal keys are routing guides, not data. The parent separator defines the boundary
between two children. Moving a key directly would break the routing invariant.
The 3-way rotation (sibling → parent → current) keeps all boundaries consistent.

**Internal Merge:**
```
Before:  Parent: [..., K_sep, ...]
         Left: [...]  Right: [...]
After:   Parent: [...] (K_sep removed, may cascade)
         Left: [..., K_sep, ...right's entries...]
                     ↑ separator PULLED DOWN into merged node
```
