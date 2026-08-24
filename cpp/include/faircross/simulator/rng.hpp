#pragma once

#include <cstdint>

namespace faircross {

/// Fast, deterministic 64-bit pseudo-random number generator (SplitMix64) with zero floating point operations.
class DeterministicRng {
public:
    explicit constexpr DeterministicRng(uint64_t seed) noexcept : state_(seed) {}

    uint64_t next_u64() noexcept {
        state_ += 0x9E3779B97F4A7C15ULL;
        uint64_t z = state_;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    uint64_t gen_range_u64(uint64_t min, uint64_t max) noexcept {
        if (min >= max) return min;
        uint64_t span = max - min + 1;
        return min + (next_u64() % span);
    }

    bool gen_bool_permille(uint32_t permille) noexcept {
        return (next_u64() % 1000) < permille;
    }

private:
    uint64_t state_;
};

} // namespace faircross
