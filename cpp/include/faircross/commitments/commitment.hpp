#pragma once

#include <cstdint>
#include <array>
#include <string>
#include <iomanip>
#include <sstream>
#include <compare>

namespace faircross {

/// A 32-byte cryptographic commitment digest.
struct Commitment {
    std::array<uint8_t, 32> bytes;

    constexpr Commitment() noexcept : bytes{} {}
    explicit constexpr Commitment(std::array<uint8_t, 32> b) noexcept : bytes(b) {}

    static constexpr Commitment zero() noexcept { return Commitment(std::array<uint8_t, 32>{}); }
    static constexpr Commitment from_bytes(std::array<uint8_t, 32> b) noexcept { return Commitment(b); }

    [[nodiscard]] const uint8_t* data() const noexcept { return bytes.data(); }
    [[nodiscard]] size_t size() const noexcept { return bytes.size(); }

    std::string to_hex() const {
        std::ostringstream oss;
        for (uint8_t b : bytes) {
            oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
        }
        return oss.str();
    }

    auto operator<=>(const Commitment&) const = default;
};

} // namespace faircross
