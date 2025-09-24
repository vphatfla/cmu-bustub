#include "primer/count_min_sketch.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace bustub{

template <typename KeyType>
CountMinSketch<KeyType>::CountMinSketch(uint32_t width, uint32_t depth) : width_(width), depth_(depth) {
    array_ = new uint32_t*[depth];
    for (size_t i = 0; i< depth; i+=1) {
        array_[i] = new uint32_t[width];
    }

    hash_functions_.reserve(depth_);
    for (size_t i =0; i < depth_; i+=1) {
        hash_functions_.push_back(this->HashFunction(i));
    }
}

template <typename KeyType>
CountMinSketch<KeyType>::CountMinSketch(CountMinSketch &&other) noexcept : width_(other.width_), depth_(other.depth_) {
    array_ = other.array_;
    hash_functions_ = other.hash_functions_;

    other.array_ = nullptr;
    other.hash_functions_ = nullptr;
}

template <typename KeyType>
auto CountMinSketch<KeyType>::operator=(CountMinSketch &&other) noexcept -> CountMinSketch & {
    if (this != &other) {
        delete array_;
        array_ = other.array_;
        other.array_ = nullptr;
    }
    return *this;
}

template <typename KeyType>
void CountMinSketch<KeyType>::Insert(const KeyType &item) {
    for (size_t row = 0; row < depth_; row += 1) {
        size_t col = hash_functions_[row](item);
        array_[row][col] += 1;
    }
}

template <typename KeyType>
auto CountMinSketch<KeyType>::Count(const KeyType &item) const -> uint32_t {
    uint32_t res = array_[0][hash_functions_[0](item)];
    for (int row = 1; row < depth_; row+=1) {
        res = std::min(res, hash_functions_[row][hash_functions_[row]](item));
    }
    return res;
}

};
