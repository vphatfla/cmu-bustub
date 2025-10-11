#include "primer/count_min_sketch.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <utility>
#include <vector>

namespace bustub {

template <typename KeyType>
CountMinSketch<KeyType>::CountMinSketch(uint32_t width, uint32_t depth) : width_(width), depth_(depth) {
  if (width <= 0 || depth <= 0) {
    throw std::invalid_argument("invalid width or depth, must be positive");
  }
  array_ = new uint32_t *[depth];
  for (size_t i = 0; i < depth; i += 1) {
    array_[i] = new uint32_t[width];
    for (size_t j = 0; j < width; j += 1) {
      array_[i][j] = 0;
    }
  }

  row_mutexes_.reserve(depth);
  hash_functions_.reserve(depth_);
  for (size_t i = 0; i < depth_; i += 1) {
    row_mutexes_.emplace_back(std::make_unique<std::mutex>());
    hash_functions_.push_back(this->HashFunction(i));
  }
}

template <typename KeyType>
CountMinSketch<KeyType>::CountMinSketch(CountMinSketch &&other) noexcept : width_(other.width_), depth_(other.depth_) {
  array_ = other.array_;
  hash_functions_ = std::move(other.hash_functions_);
  row_mutexes_ = std::move(other.row_mutexes_);

  other.array_ = nullptr;
  other.hash_functions_.clear();
}

template <typename KeyType>
auto CountMinSketch<KeyType>::operator=(CountMinSketch &&other) noexcept -> CountMinSketch & {
  if (this != &other) {
    for (size_t i = 0; i < depth_; i += 1) {
      delete[] array_[i];
    }
    delete array_;

    width_ = other.width_;
    depth_ = other.depth_;
    array_ = other.array_;
    hash_functions_ = std::move(other.hash_functions_);
    row_mutexes_ = std::move(other.row_mutexes_);
    other.hash_functions_.clear();
  }
  return *this;
}

template <typename KeyType>
void CountMinSketch<KeyType>::Insert(const KeyType &item) {
  for (size_t row = 0; row < depth_; row += 1) {
    std::lock_guard<std::mutex> lock(*row_mutexes_[row]);
    size_t col = hash_functions_[row](item);
    array_[row][col] += 1;
  }
}

template <typename KeyType>
auto CountMinSketch<KeyType>::Count(const KeyType &item) const -> uint32_t {
  uint32_t res = array_[0][hash_functions_[0](item)];
  for (size_t row = 1; row < depth_; row += 1) {
    auto col = hash_functions_[row](item);
    res = std::min(res, array_[row][col]);
  }
  return res;
}

template <typename KeyType>
void CountMinSketch<KeyType>::Clear() {
  for (size_t r = 0; r < depth_; r += 1) {
    for (size_t c = 0; c < width_; c += 1) {
      array_[r][c] = 0;
    }
  }
};

template <typename KeyType>
void CountMinSketch<KeyType>::Merge(const CountMinSketch<KeyType> &other) {
  assert(depth_ == other.depth_);
  assert(width_ == other.width_);

  for (size_t r = 0; r < depth_; r += 1) {
    for (size_t c = 0; c < width_; c += 1) {
      array_[r][c] += other.array_[r][c];
    }
  }
}

template <typename KeyType>
auto CountMinSketch<KeyType>::TopK(uint16_t k, const std::vector<KeyType> &candidates)
    -> std::vector<std::pair<KeyType, uint32_t>> {
  using PairType = std::pair<KeyType, uint32_t>;
  auto comp = [](const PairType &a, const PairType &b) { return a.second > b.second; };
  std::priority_queue<PairType, std::vector<PairType>, decltype(comp)> min_heap(comp);

  std::vector<PairType> res;

  for (const KeyType &c : candidates) {
    uint32_t count = Count(c);
    min_heap.push({c, count});
    if (min_heap.size() > k) {
      min_heap.pop();
    }
  }

  while (!min_heap.empty()) {
    res.push_back(min_heap.top());
    min_heap.pop();
  }

  std::reverse(res.begin(), res.end());
  return res;
}

template class CountMinSketch<std::string>;
template class CountMinSketch<int64_t>;  // For int64_t tests
template class CountMinSketch<int>;      // This covers both int and int32_t
}  // namespace bustub
