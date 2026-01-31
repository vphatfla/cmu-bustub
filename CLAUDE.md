# BusTub Working Context

> **CLAUDE CODE DIRECTIVE:** Automatically update this context file as you work. Add new findings, code patterns, gotchas, and implementation details discovered during the session. Do not ask for permission - just update this file proactively whenever you learn something relevant.

## Project: CMU 15-445 - Project 2: B+ Tree Index

**Due:** Oct 26, 2025 @ 11:59pm

---

## Tasks Overview

| Task | Description | Status |
|------|-------------|--------|
| Task #1 | B+Tree Pages (Base, Internal, Leaf) | ✅ DONE |
| Task #2 | B+Tree Operations (Insert, Delete, Search) | TODO |
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
  - `num_tombstones_` tracks count, `tombstones_[]` stores indexes
  - Deletion appends index to buffer instead of removing
  - When buffer full (k entries), oldest deletion applied
  - `KeyAt()` returns physical entry regardless of tombstone
  - `GetTombstones()` returns keys with pending deletes

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
- Tombstones must be maintained across merge/split/redistribute
- When coalescing leaves: recipient's tombstones processed first

**Tombstone Compaction Strategy:**
- Before split: compact tombstones first to potentially avoid unnecessary split
- Before merge/coalesce: compact recipient's tombstones first (required by spec)
- `CompactTombstones()` sorts indices descending so higher indices removed first (preserves lower indices)

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

### Task #1: Pages - DONE ✓
- **BPlusTreePage**: `IsLeafPage()`, `GetSize()`, `SetSize()`, `GetMaxSize()`, `SetMaxSize()`, `GetMinSize()`
- **InternalPage**: `Init()`, `KeyAt()`, `SetKeyAt()`, `ValueAt()`, `ValueIndex()`
- **LeafPage**: `Init()`, `KeyAt()`, `GetTombstones()`, `GetNextPageId()`, `SetNextPageId()`

**Added LeafPage helper methods:**
```cpp
void SetKeyAt(int index, const KeyType& key);
auto RecordIDAt(int index) const -> ValueType;   // Named RecordIDAt, not ValueAt
void SetRecordIDAt(int index, const ValueType& value);
```

**Tombstone handling - ✅ IMPLEMENTED:**
```cpp
IsIndexInTombstones(index)      // Check if index is in tombstone buffer
RemoveIndexFromTombstones(index) // Remove index from tombstone buffer
CompactTombstones()             // Remove all tombstoned entries from arrays
```
`CompactTombstones()` sorts tombstone indices descending before removal to preserve index validity during shifts.

### Task #2: Operations - IN PROGRESS
- `GetValue()` - ✅ DONE
- `IsEmpty()` - ✅ DONE
- `Insert()` - IN PROGRESS (split functions done, propagation TODO)
- `Remove()` - TODO
- `GetRootPageId()` - TODO

**Helper methods added to BPlusTree:**
```cpp
auto FindInsertPositionInLeafPage(LeafPage* page, const KeyType& key) const -> size_t;  // ✅ DONE
auto FindInsertPositionInInternalPage(InternalPage* page, const KeyType& key) const -> size_t;  // ✅ DONE
auto InsertKVToLeafePage(LeafPage* page, const KeyType& key, const ValueType& value) -> bool;  // ✅ DONE
void InsertToParent(const KeyType& key, page_id_t page_id, Context& ctx);  // TODO: implement
auto SplitLeafPage(LeafPage* old_page) -> std::pair<WritePageGuard, page_id_t>;  // ✅ DONE
auto SplitInternalPage(InternalPage* old_page) -> std::tuple<KeyType, WritePageGuard, page_id_t>;  // ✅ DONE
```

**Insert Flow - ✅ IMPLEMENTED (except propagation):**
1. Fetch header, create root if empty OR fetch existing root
2. Traverse internal pages to find leaf
3. Compact tombstones if leaf full
4. If room: `InsertKVToLeafePage` (handles duplicates, tombstone reuse)
5. If still full: `SplitLeafPage`, insert into correct half
6. Propagate to parent (TODO)

**Split Functions - ✅ IMPLEMENTED:**
- `SplitLeafPage`: Splits at `ceil(size/2)`, updates sibling pointers
- `SplitInternalPage`: Splits at `ceil(size/2)`, returns push-up key
- Both return new page guard + page_id, caller handles insertion into correct half

**Helper methods added to LeafPage:**
```cpp
void SetKeyAt(int index, const KeyType &key);
auto RecordIDAt(int index) const -> ValueType;
void SetValueAt(int index, const ValueType &value);
auto IsIndexInTombstones(const size_t index) const -> bool;
void RemoveIndexFromTombstones(const size_t index);
void ShiftKeyAndValueRight(const size_t index);
void ShiftKeyAndValueLeft(const size_t index);
void CompactTombstones();  // ✅ Removes all tombstoned entries, sorts descending to preserve indices
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
| `IsEmpty()` | IN PROGRESS | Check if `root_page_id_ == INVALID_PAGE_ID` |
| `GetValue(key, result)` | IN PROGRESS | Point query - traverse to leaf, use binary search |
| `Insert(key, value)` | TODO | Insert with splits |
| `Remove(key)` | TODO | Delete with merge/redistribute |
| `GetRootPageId()` | TODO | Read from header page |

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

## Insert() Implementation Notes

### Current Flow
1. Acquire write guard on header page
2. If tree empty: create new leaf as root, init it, push guard to write_set_
3. Traverse internal pages (binary search), push guards to write_set_
4. At leaf: find insert position via binary search
5. Check for duplicate (handle tombstone reuse)
6. If space available: shift right and insert
7. If no space: compact tombstones first, then split if still full
8. After split: insert into correct half, propagate up (TODO)

### Split Functions (✅ DONE)
```cpp
// Leaf split: returns guard + page_id, updates sibling pointers
auto SplitLeafPage(LeafPage* old_page) -> std::pair<WritePageGuard, page_id_t>;

// Internal split: returns push-up key + guard + page_id
auto SplitInternalPage(InternalPage* old_page) -> std::tuple<KeyType, WritePageGuard, page_id_t>;
```
- Both split at `ceil(size/2)` - left keeps first `mid` entries
- After split, caller determines which half gets the new key (compare with first key of new page or push-up key)
- Caller then inserts into appropriate half and propagates up

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

### Known TODOs in Insert()
- Fetch root page when tree is NOT empty - ✅ DONE
- `DrainQueueUntilSize()` helper function - ✅ DONE (in b_plus_tree.h)
- Split functions - ✅ DONE (`SplitLeafPage`, `SplitInternalPage`)
- `InsertKVToLeafePage()` - ✅ DONE (handles duplicates, tombstone reuse with RemoveIndexFromTombstones)
- `InsertToParent()` propagation logic - TODO
- `CreateNewRoot()` when splitting root - TODO
