# BusTub Working Context

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

### Task #2: Operations - TODO
- `Insert()`, `Remove()`, `GetValue()`, `GetRootPageId()`

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
