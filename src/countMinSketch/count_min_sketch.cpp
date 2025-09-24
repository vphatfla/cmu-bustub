#include <algorithm>
#include <cstddef>
#include <iterator>
#include "count_min_sketch.h"
namespace bustub {

template <typename KeyType>
CountMinSketch<KeyType>::CountMinSketch(uint32_t width, uint32_t depth): _width(width), _depth(depth) {
    _array = new KeyType*[depth];

    for (size_t i = 0; i < depth; i += 1) {
        _array[i] = new KeyType[depth];
    }
}

template <typename KeyType>
CountMinSketch<KeyType>::CountMinSketch(CountMinSketch&& other) noexcept: _width(other._width), _depth(other._depth){
    _array = std::move(other._array);
}

template <typename KeyType>
CountMinSketch<KeyType>::operator=(CountMinSketch&& other) noexcept -> CountMinSketch & {
    return *this;
}

template <typename KeyType>
CountMinSketch<KeyType>::Insert(const KeyType &item) {
    
}
}
