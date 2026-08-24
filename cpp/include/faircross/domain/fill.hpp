#pragma once

#include "faircross/domain/primitives.hpp"
#include "faircross/domain/order.hpp"

namespace faircross {

/// Execution fill result representing a matched trade for an order.
struct Fill {
    uint64_t fill_id;
    OrderId order_id;
    AccountId account_id;
    InstrumentId instrument_id;
    Side side;
    Price execution_price;
    Qty fill_qty;
    Money consideration;

    static Result<Fill> create(
        uint64_t fill_id,
        OrderId order_id,
        AccountId account_id,
        InstrumentId instrument_id,
        Side side,
        Price execution_price,
        Qty fill_qty
    ) {
        auto cons_res = Money::from_price_qty(execution_price, fill_qty);
        if (cons_res.is_err()) return cons_res.error_message();
        return Fill{
            fill_id,
            order_id,
            account_id,
            instrument_id,
            side,
            execution_price,
            fill_qty,
            cons_res.value()
        };
    }

    auto operator<=>(const Fill&) const = default;
};

} // namespace faircross
