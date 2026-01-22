# BusTub Working Context

## Project: CMU 15-445 - Project 2: B+ Tree Index

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                    BPlusTree (b_plus_tree.h/cpp)            │
│         Insert() / Remove() / GetValue() / Begin()         │
│                    (Tree operations logic)                  │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                      Page Classes                           │
│              (Data containers - just accessors)             │
│                                                             │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────┐ │
│  │ BPlusTreePage   │  │ InternalPage    │  │ LeafPage    │ │
│  │ (base class)    │◄─┤ (keys + ptrs)   │  │ (keys+vals) │ │
│  │                 │  └─────────────────┘  └─────────────┘ │
│  └─────────────────┘                                        │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                   BufferPoolManager                         │
│            FetchPage() / NewPage() / UnpinPage()           │
└─────────────────────────────────────────────────────────────┘
```

**Key insight:** Page classes are just data containers with accessors. BPlusTree class implements all tree logic (insert, delete, split, merge, redistribute).

---

## File Locations

| Component | Header | Source |
|-----------|--------|--------|
| Base Page | `src/include/storage/page/b_plus_tree_page.h` | `src/storage/page/b_plus_tree_page.cpp` |
| Internal Page | `src/include/storage/page/b_plus_tree_internal_page.h` | `src/storage/page/b_plus_tree_internal_page.cpp` |
| Leaf Page | `src/include/storage/page/b_plus_tree_leaf_page.h` | `src/storage/page/b_plus_tree_leaf_page.cpp` |
| B+ Tree | `src/include/storage/index/b_plus_tree.h` | `src/storage/index/b_plus_tree.cpp` |
| Iterator | `src/include/storage/index/index_iterator.h` | `src/storage/index/index_iterator.cpp` |

---

## Page Specifications

### Base Page (BPlusTreePage)
**Header: 12 bytes**
| Field | Size | Description |
|-------|------|-------------|
| page_type_ | 4 | INVALID_INDEX_PAGE / LEAF_PAGE / INTERNAL_PAGE |
| size_ | 4 | Number of key/value pairs |
| max_size_ | 4 | Max key/value pairs |

### Internal Page
- Stores **n keys** and **n child pointers** (page_ids)
- **key[0] is INVALID** - lookups start from index 1
- Layout:
  ```
  key_array_:     [INVALID] [key1] [key2] ... [key_n-1]
  page_id_array_: [ptr0]    [ptr1] [ptr2] ... [ptr_n-1]
  ```
- Invariant: `K(i) <= keys_in_subtree(ptr_i) < K(i+1)`

### Leaf Page
- Stores **m keys** and **m values** (RIDs)
- Values are 64-bit record IDs (see `src/include/common/rid.h`)
- **Tombstone buffer** for lazy deletion (Bε-tree concept):
  - Stores last k indexes of deleted entries
  - Deletion appends to buffer instead of removing
  - When buffer full, oldest deletion applied

---

## Template Macros

```cpp
// Shorthand for template declarations
#define INDEX_TEMPLATE_ARGUMENTS \
  template <typename KeyType, typename ValueType, typename KeyComparator>

// Usage:
INDEX_TEMPLATE_ARGUMENTS
auto BPlusTreeInternalPage<...>::KeyAt(int index) -> KeyType { ... }
```

| Parameter | Purpose |
|-----------|---------|
| KeyType | Key type (e.g., GenericKey<8>) |
| ValueType | Value type (page_id_t for internal, RID for leaf) |
| KeyComparator | Comparison functor |

---

## Memory Model

Pages use **embedded arrays** (not std::vector):
```cpp
KeyType key_array_[SLOT_CNT];      // Fixed-size, inline
ValueType value_array_[SLOT_CNT];  // Fixed-size, inline
```

**Why:** Pages are accessed via `reinterpret_cast` from buffer pool memory. No constructors run, so heap-allocated containers would be garbage pointers.

**Usage pattern:**
```cpp
auto page = bpm->FetchPage(page_id);
auto internal = reinterpret_cast<InternalPage*>(page->GetData());
// ... use internal->KeyAt(), etc ...
bpm->UnpinPage(page_id, is_dirty);
```

---

## Implementation Status

### BPlusTreePage (Base) - DONE ✓
- `IsLeafPage()`, `SetPageType()`
- `GetSize()`, `SetSize()`, `ChangeSizeBy()`
- `GetMaxSize()`, `SetMaxSize()`, `GetMinSize()`

### Internal Page - DONE ✓
- `Init(max_size)` - sets type, max_size, size=0
- `KeyAt(index)` - returns key_array_[index]
- `SetKeyAt(index, key)` - sets key_array_[index]
- `ValueAt(index)` - returns page_id_array_[index]
- `ValueIndex(value)` - linear search, UNREACHABLE if not found

### Leaf Page - TODO
- `Init()`, `KeyAt()`, `SetKeyAt()`, `ValueAt()`, `SetValueAt()`
- `GetTombstones()` - return keys corresponding to tombstones
- Tombstone buffer logic

### BPlusTree - TODO
- `Insert()` - find leaf, insert, split if full
- `Remove()` - find leaf, delete, merge/redistribute if underfull
- `GetValue()` - traverse to leaf, return value
- `Begin()` / `End()` - iterator support

---

## Context Class (for tree operations)

```cpp
class Context {
  std::optional<WritePageGuard> header_page_;  // Lock on header
  page_id_t root_page_id_;                     // Current root
  std::deque<WritePageGuard> write_set_;       // Pages being modified
  std::deque<ReadPageGuard> read_set_;         // Pages being read
};
```

Used to track page locks during tree traversal (crabbing protocol).

---

## Quick Reference

**Slot count calculation:**
```cpp
#define INTERNAL_PAGE_SLOT_CNT \
  ((BUSTUB_PAGE_SIZE - HEADER_SIZE) / (sizeof(KeyType) + sizeof(ValueType)))
```

**Page size:** 4096 bytes (typical)

**Min size:** `floor(max_size / 2)` - for merge/redistribute decisions
