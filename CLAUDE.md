# BusTub Working Context

## Project: CMU 15-445 - Project 2: B+ Tree Index

---

## Project Specification

### Base Page (BPlusTreePage)
- **Files**: `b_plus_tree_page.h`, `b_plus_tree_page.cpp`
- **Header (12 bytes)**:
  | Field | Size | Description |
  |-------|------|-------------|
  | page_type_ | 4 | invalid / leaf / internal |
  | size_ | 4 | Number of key/value pairs |
  | max_size_ | 4 | Max key/value pairs |

---

### Internal Page
- **Files**: `b_plus_tree_internal_page.h`, `b_plus_tree_internal_page.cpp`
- Stores **m ordered keys** and **m+1 child pointers** (page_ids)
- Represented as array of key/page_id pairs
- **IMPORTANT**: First key in `key_array_` is INVALID - lookups start from second key
- Must be at least **half full** at all times
- Operations: merge, redistribute, split

**Layout:**
```
key_array_:     [INVALID] [key1] [key2] ... [key_n-1]
page_id_array_: [ptr0]    [ptr1] [ptr2] ... [ptr_n-1]
```
- n keys stored, but key[0] is invalid
- n pointers stored
- size_ = n (number of entries)

---

### Leaf Page
- **Files**: `b_plus_tree_leaf_page.h`, `b_plus_tree_leaf_page.cpp`
- Stores **m ordered keys** and **m corresponding values**
- Values are **64-bit RIDs** (record ids) - see `src/include/common/rid.h`
- Same half-full restrictions as internal pages

#### Tombstone Buffer (Bε-tree concept)
- Stores last **k indexes** of deleted entries
- When key is deleted (if k > 0):
  - Entry is NOT actually removed
  - Index is appended to tombstone buffer
- When buffer has k entries:
  - Oldest buffered deletion is actually applied to key/value arrays
- **GetTombstones()**: Returns keys that tombstones correspond to
- **KeyAt()**: Returns physical entry at index (regardless of tombstone status)

---

### Key Implementation Notes
1. Leaf and Internal pages have **same key type but different value types**
2. `max_size` can differ between leaf and internal pages
3. Pages correspond to `data_` part of memory page from buffer pool
4. **Usage pattern**:
   - Fetch page from buffer pool (using page_id)
   - `reinterpret_cast` to leaf or internal page
   - Read/write operations
   - Unpin page after use

---

## Implementation Status

### BPlusTreePage (Base) - DONE
- `IsLeafPage()` - checks `page_type_ == LEAF_PAGE`
- `SetPageType(IndexPageType)` - sets page type
- `GetSize()` / `SetSize()` / `ChangeSizeBy()` - size management
- `GetMaxSize()` / `SetMaxSize()` - capacity management
- `GetMinSize()` - returns `floor(max_size_ / 2)`

### Internal Page - ALMOST DONE

**Current implementation:**
```cpp
Init(max_size):
  - SetPageType(IndexPageType::INTERNAL_PAGE)  // OK (fixed)
  - SetMaxSize(max_size)                       // OK
  - SetSize(0)                                 // OK (fixed)

KeyAt(index):     return key_array_[index]       // OK
SetKeyAt(index):  key_array_[index] = key        // OK
ValueAt(index):   return page_id_array_[index]   // OK
ValueIndex(value): linear search                 // HAS BUGS
```

**Remaining bug in ValueIndex (lines 78-86):**
1. Loop uses `INTERNAL_PAGE_SLOT_CNT` - searches uninitialized memory
   - Should use `GetSize()`
2. `BUSTUB_ENSURE(true, ...)` - always passes (true is always true)
   - Should be `UNREACHABLE("msg")` or `BUSTUB_ENSURE(false, ...)`

### Leaf Page - TODO

---

## Notes
- Embedded arrays (not std::vector) because pages are reinterpret_cast from raw memory
- No constructors run, data must be physically in the 4KB page
