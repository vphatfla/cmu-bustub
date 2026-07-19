#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <utility>

#include "common/config.h"
#include "common/exception.h"
#include "storage/table/tuple.h"

namespace bustub {

/**
 * Page to hold the intermediate data for external merge sort and hash join.
 * Supports variable-length tuples.
 */
class IntermediateResultPage {
 public:
  /**
   * Init the header of this page
   */
  void Init() {
    header_.next_page_id_ = INVALID_PAGE_ID;
    header_.num_tuples_ = 0;
  }

  /*
   * Get the number of tuples in this page
   */
  auto inline GetNumTuples() const -> uint16_t { return header_.num_tuples_; }

  /*
   * Get the tuple at position given index
   * if such tuple does not exist, throw exception
   */
  auto inline GetTupleAtIndex(const uint16_t index) const -> Tuple {
    if (index >= header_.num_tuples_) {
      throw bustub::Exception("Tuple index out of range");
    }

    auto &[tuple_begin_offset, tuple_size] = tuple_info_[index];

    return Tuple{RID{}, page_begin_offset_ + tuple_begin_offset, tuple_size};
  }
  /*
   * @brief Get the Next tuple offset
   * @return optional offset, if no more space then will be nulloptional
   */
  auto inline GetNextTupleOffset(const Tuple &tuple) const -> std::optional<uint16_t> {
    size_t new_tuple_end_offset;

    if (header_.num_tuples_ > 0) {
      new_tuple_end_offset = tuple_info_[header_.num_tuples_ - 1].first;
    } else {
      new_tuple_end_offset = BUSTUB_PAGE_SIZE;
    }

    // guard against unsigned underflow when the tuple is larger than the remaining space
    if (tuple.GetLength() > new_tuple_end_offset) {
      return std::nullopt;
    }

    auto new_tuple_begin_offset = new_tuple_end_offset - tuple.GetLength();

    auto new_tuple_info_end_offset = INTERMEDIATE_PAGE_HEADER_SIZE + sizeof(TupleInfo) * (header_.num_tuples_ + 1);

    if (new_tuple_begin_offset < new_tuple_info_end_offset) {
      return std::nullopt;
    }

    return static_cast<uint16_t>(new_tuple_begin_offset);
  }

  /*
   * @brief append the tuple into the page
   * @return TRUE if append was successfully, FALSE if Append failed because there was not enough space in the frame
   * similar to InsertTuple
   */
  auto inline InsertTuple(const Tuple &tuple) -> bool {
    auto new_tuple_begin_offset = GetNextTupleOffset(tuple);
    if (!new_tuple_begin_offset.has_value()) {
      return false;
    }

    auto new_tuple_id = header_.num_tuples_;

    tuple_info_[new_tuple_id] = TupleInfo{new_tuple_begin_offset.value(), tuple.GetLength()};

    memcpy(page_begin_offset_ + new_tuple_begin_offset.value(), tuple.GetData(), tuple.GetLength());

    header_.num_tuples_ += 1;
    return true;
  }

  static constexpr size_t INTERMEDIATE_PAGE_HEADER_SIZE = 8;

 private:
  /* helper struct */
  struct Header {
    page_id_t next_page_id_;
    uint16_t num_tuples_;
  };

  /* store the info of the tuple and its possition within the page (4 bytes)
   * pair.first = start offset
   * pair.second = len of this referred tuple
   */
  using TupleInfo = std::pair<uint16_t, uint16_t>;

  /* label position tracking for the begin offset of the whole page, cost 0 bytes space */
  char page_begin_offset_[0];

  /* header of page */
  Header header_;
  /* end header of page */

  /* position label for the tuple info of each tuple within this page, zero cost storage for this page */
  TupleInfo tuple_info_[0];

  /* static member and static check, static members does not belong to the object data and won't be written to disk */
  static constexpr size_t TUPLE_INFO_SIZE = 4;
  static_assert(TUPLE_INFO_SIZE == sizeof(TupleInfo));

  static_assert(INTERMEDIATE_PAGE_HEADER_SIZE == sizeof(Header));
};

/* an empty init page should be 8 */
static_assert(IntermediateResultPage::INTERMEDIATE_PAGE_HEADER_SIZE == sizeof(IntermediateResultPage));

}  // namespace bustub
