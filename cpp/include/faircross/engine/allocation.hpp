#pragma once

#include <vector>
#include <optional>
#include "faircross/domain/primitives.hpp"
#include "faircross/domain/order.hpp"
#include "faircross/engine/batch.hpp"

namespace faircross {

/// The execution allocation for a single order in a batch.
struct OrderAllocation {
    OrderId order_id;
    AccountId account;
    Side side;
    Price limit_price;
    Qty original_qty;
    Qty allocated_qty;
    uint64_t seq;

    auto operator<=>(const OrderAllocation&) const = default;
};

/// The complete allocation result across all orders in a batch.
struct BatchAllocation {
    std::optional<Price> clearing_price;
    Qty target_volume;
    Qty total_buy_allocated;
    Qty total_sell_allocated;
    std::vector<OrderAllocation> allocations;

    auto operator<=>(const BatchAllocation&) const = default;
};

Result<std::vector<OrderAllocation>> allocate_side(
    const std::vector<Order>& orders,
    Price clearing_price,
    Qty target_volume,
    Side target_side
);

Result<BatchAllocation> allocate_batch(
    const Batch& batch,
    std::optional<Price> clearing_price,
    Qty target_volume
);

} // namespace faircross
