#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>
#include "common/util/hash_util.h"

namespace bustub {

template <typename KeyType>
class CountMinSketch {
public:
    explicit CountMinSketch(uint32_t width, uint32_t depth);

    CountMinSketch() = delete; // default constructor
    CountMinSketch(const CountMinSketch &) = delete;
    auto operator=(const CountMinSketch &) -> CountMinSketch & = delete;

    CountMinSketch(CountMinSketch &&other) noexcept; // move constructor
    auto operator=(CountMinSketch &&other) noexcept -> CountMinSketch &; // move assignment

    void Insert(const KeyType &item);

    auto Count(const KeyType &item) const -> uint32_t;

    void Clear();

    void Merge(const CountMinSketch<KeyType> &other);

    /**
        @brief Gets the top k items based on estimated counts from a list of candidates
     **/
    auto TopK(uint16_t k, const std::vector<KeyType> &candidates) -> std::vector<std::pair<KeyType, uint32_t>>;

private:
    uint32_t width_;
    uint32_t depth_;
    uint32_t **array_;

    std::vector<std::function<size_t(const KeyType &)>> hash_functions_;

    /** @fall2025 PLEASE DO NOT MODIFY THE FOLLOWING */
    constexpr static size_t SEED_BASE = 15445;

    inline auto HashFunction(size_t seed) -> std::function<size_t(const KeyType &)> {
        return [seed, this](const KeyType &item) -> size_t {
            auto h1 = std::hash<KeyType>{}(item);
            auto h2 = bustub::HashUtil::CombineHashes(seed, SEED_BASE);
            return bustub::HashUtil::CombineHashes(h1, h2) % width_;
        };
    }
};
}
