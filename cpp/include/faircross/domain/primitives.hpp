#pragma once

#include <cstdint>
#include <string>
#include <limits>
#include <compare>
#include "faircross/domain/error.hpp"

namespace faircross {

/// An integer price tick representing a strictly positive price multiple.
class Price {
public:
    static constexpr uint64_t MIN_RAW = 1;
    static constexpr uint64_t MAX_RAW = std::numeric_limits<uint64_t>::max();

    constexpr Price() noexcept : raw_(1) {}
    explicit constexpr Price(uint64_t raw) noexcept : raw_(raw) {}

    static Result<Price> create(uint64_t raw) {
        if (raw == 0) {
            return PrimitiveError{PrimitiveErrorKind::ZeroValue, "Price"};
        }
        return Price(raw);
    }

    [[nodiscard]] constexpr uint64_t as_raw() const noexcept { return raw_; }

    [[nodiscard]] Result<Price> checked_add(Price other) const {
        if (raw_ > MAX_RAW - other.raw_) {
            return PrimitiveError{PrimitiveErrorKind::ArithmeticOverflow, "Price::checked_add"};
        }
        return Price(raw_ + other.raw_);
    }

    [[nodiscard]] Result<Price> checked_sub(Price other) const {
        if (raw_ <= other.raw_) {
            if (raw_ == other.raw_) {
                return PrimitiveError{PrimitiveErrorKind::ZeroValue, "Price"};
            }
            return PrimitiveError{PrimitiveErrorKind::InsufficientBalance, "Price"};
        }
        return Price(raw_ - other.raw_);
    }

    auto operator<=>(const Price&) const = default;

private:
    uint64_t raw_;
};

/// An integer quantity of lots or units.
class Qty {
public:
    static constexpr uint64_t MAX_RAW = std::numeric_limits<uint64_t>::max();

    constexpr Qty() noexcept : raw_(0) {}
    explicit constexpr Qty(uint64_t raw) noexcept : raw_(raw) {}

    static constexpr Qty zero() noexcept { return Qty(0); }
    static constexpr Qty from_raw(uint64_t raw) noexcept { return Qty(raw); }

    static Result<Qty> create(uint64_t raw) {
        if (raw == 0) {
            return PrimitiveError{PrimitiveErrorKind::ZeroValue, "Qty"};
        }
        return Qty(raw);
    }

    [[nodiscard]] constexpr uint64_t as_raw() const noexcept { return raw_; }
    [[nodiscard]] constexpr bool is_zero() const noexcept { return raw_ == 0; }

    [[nodiscard]] Result<Qty> checked_add(Qty other) const {
        if (raw_ > MAX_RAW - other.raw_) {
            return PrimitiveError{PrimitiveErrorKind::ArithmeticOverflow, "Qty::checked_add"};
        }
        return Qty(raw_ + other.raw_);
    }

    [[nodiscard]] Result<Qty> checked_sub(Qty other) const {
        if (raw_ < other.raw_) {
            return PrimitiveError{PrimitiveErrorKind::InsufficientBalance, "Qty"};
        }
        return Qty(raw_ - other.raw_);
    }

    [[nodiscard]] constexpr Qty saturating_sub(Qty other) const noexcept {
        if (raw_ < other.raw_) return Qty(0);
        return Qty(raw_ - other.raw_);
    }

    auto operator<=>(const Qty&) const = default;

private:
    uint64_t raw_;
};

/// An integer monetary amount stored in atomic/base currency units (128-bit).
class Money {
public:
    using RawType = unsigned __int128;
    static constexpr RawType MAX_RAW = ~RawType(0);

    constexpr Money() noexcept : raw_(0) {}
    explicit constexpr Money(RawType raw) noexcept : raw_(raw) {}

    static constexpr Money zero() noexcept { return Money(0); }
    static constexpr Money from_raw(RawType raw) noexcept { return Money(raw); }

    [[nodiscard]] constexpr RawType as_raw() const noexcept { return raw_; }
    [[nodiscard]] constexpr bool is_zero() const noexcept { return raw_ == 0; }

    static Result<Money> from_price_qty(Price price, Qty qty) {
        RawType p = price.as_raw();
        RawType q = qty.as_raw();
        if (p != 0 && q > MAX_RAW / p) {
            return PrimitiveError{PrimitiveErrorKind::ArithmeticOverflow, "Money::from_price_qty"};
        }
        return Money(p * q);
    }

    [[nodiscard]] Result<Money> checked_add(Money other) const {
        if (raw_ > MAX_RAW - other.raw_) {
            return PrimitiveError{PrimitiveErrorKind::ArithmeticOverflow, "Money::checked_add"};
        }
        return Money(raw_ + other.raw_);
    }

    [[nodiscard]] Result<Money> checked_sub(Money other) const {
        if (raw_ < other.raw_) {
            return PrimitiveError{PrimitiveErrorKind::InsufficientBalance, "Money"};
        }
        return Money(raw_ - other.raw_);
    }

    auto operator<=>(const Money&) const = default;

private:
    RawType raw_;
};

/// Strongly typed 64-bit Order ID.
class OrderId {
public:
    constexpr OrderId() noexcept : raw_(0) {}
    explicit constexpr OrderId(uint64_t raw) noexcept : raw_(raw) {}
    [[nodiscard]] constexpr uint64_t as_raw() const noexcept { return raw_; }
    auto operator<=>(const OrderId&) const = default;
private:
    uint64_t raw_;
};

/// Strongly typed 64-bit Account ID.
class AccountId {
public:
    constexpr AccountId() noexcept : raw_(0) {}
    explicit constexpr AccountId(uint64_t raw) noexcept : raw_(raw) {}
    [[nodiscard]] constexpr uint64_t as_raw() const noexcept { return raw_; }
    auto operator<=>(const AccountId&) const = default;
private:
    uint64_t raw_;
};

/// Strongly typed 64-bit Instrument ID.
class InstrumentId {
public:
    constexpr InstrumentId() noexcept : raw_(0) {}
    explicit constexpr InstrumentId(uint64_t raw) noexcept : raw_(raw) {}
    [[nodiscard]] constexpr uint64_t as_raw() const noexcept { return raw_; }
    auto operator<=>(const InstrumentId&) const = default;
private:
    uint64_t raw_;
};

/// Strongly typed 64-bit Batch ID.
class BatchId {
public:
    constexpr BatchId() noexcept : raw_(0) {}
    explicit constexpr BatchId(uint64_t raw) noexcept : raw_(raw) {}
    [[nodiscard]] constexpr uint64_t as_raw() const noexcept { return raw_; }
    auto operator<=>(const BatchId&) const = default;
private:
    uint64_t raw_;
};

} // namespace faircross
