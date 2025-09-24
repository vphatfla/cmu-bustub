#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>
#include "common/util/hash_util.h"

namespace bustub {

template <typename KeyType>
class CountMinSketch {
private:
    int** _array;
    uint32_t width_;
    uint32_t depth_;

    // pre-computed hash functions for each row
    std::vector<std::function<size_t(const KeyType &)>> hash_functions_;

    /** @fall2025 PLEASE DO NOT MODIFY THE FOLLOWING */
    constexpr static size_t SEED_BASE = 15445;

    // hash function generator
    inline auto HashFunction(size_t seed) -> std::function<size_t(const KeyType &)> {
        return [seed, this](const KeyType &item) -> size_t {
            auto h1 = std::hash<KeyType>{}(item);
            auto h2 = bustub::HashUtil::CombineHashes(seed, SEED_BASE);
            return bustub::HashUtil::CombineHashes(h1, h2) % width_;
        };
    }
public:
    explicit CountMinSketch(uint32_t width, uint32_t depth);

    // rule of five
    CountMinSketch() = delete; // default constructor deleted
    CountMinSketch(const CountMinSketch &) = delete;                      // Copy constructor deleted
    auto operator=(const CountMinSketch &) -> CountMinSketch & = delete;  // Copy assignment deleted

    CountMinSketch<KeyType>(CountMinSketch&& other) noexcept;
    auto operator=(CountMinSketch&& other) noexcept;


    void Insert(const KeyType &item);
    auto Count(const KeyType& item);
    void Clear();
    void Merge(const CountMinSketch<KeyType>& other);
    auto TopK(uint16_t k, const std::vector<KeyType> &candidates) -> std::vector<std::pair<KeyType, uint32_t>>;
};

}
