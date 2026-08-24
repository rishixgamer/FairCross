#pragma once

#include <cstdint>
#include <array>
#include "faircross/domain/primitives.hpp"

namespace faircross {

enum class Side : uint8_t {
    Buy = 1,
    Sell = 2,
};

[[nodiscard]] constexpr bool is_buy(Side s) noexcept { return s == Side::Buy; }
[[nodiscard]] constexpr bool is_sell(Side s) noexcept { return s == Side::Sell; }

/// Canonical limit order domain model.
struct Order {
    OrderId id;
    AccountId account;
    InstrumentId instrument;
    Side side;
    Price price;
    Qty qty;
    uint64_t seq;

    auto operator<=>(const Order&) const = default;
};

/// Salted order preimage for commitment hashing: Order + 32-byte Nonce.
struct SaltedOrderPreimage {
    Order order;
    std::array<uint8_t, 32> nonce;

    SaltedOrderPreimage() = default;
    SaltedOrderPreimage(Order o, std::array<uint8_t, 32> n)
        : order(o), nonce(n) {}

    auto operator<=>(const SaltedOrderPreimage&) const = default;
};

} // namespace faircross
