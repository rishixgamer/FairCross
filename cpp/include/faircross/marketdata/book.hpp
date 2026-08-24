#pragma once

#include <map>
#include <vector>
#include <optional>
#include "faircross/domain/primitives.hpp"
#include "faircross/domain/order.hpp"
#include "faircross/marketdata/messages.hpp"

namespace faircross {

struct BookLevel {
    Price price;
    Qty total_qty;
    size_t order_count;

    auto operator<=>(const BookLevel&) const = default;
};

struct BookOrder {
    uint64_t order_reference_number;
    Side side;
    Price price;
    Qty shares;
};

class OrderBook {
public:
    OrderBook() = default;

    Result<Ok> add_order(uint64_t ref_num, Side side, Price price, Qty shares);
    Result<Ok> execute_order(uint64_t ref_num, Qty shares);
    Result<Ok> cancel_order(uint64_t ref_num, Qty shares);
    Result<Ok> delete_order(uint64_t ref_num);
    Result<Ok> replace_order(uint64_t orig_ref, uint64_t new_ref, Qty new_shares, Price new_price);

    /// Applies one parsed ITCH message to the book.
    ///
    /// Administrative messages (system event, stock directory) advance the clock
    /// without touching the book.
    Result<Ok> apply_message(const ItchMessage& msg);

    [[nodiscard]] uint64_t last_timestamp_nanos() const noexcept { return last_timestamp_nanos_; }

    [[nodiscard]] std::optional<Price> best_bid() const;
    [[nodiscard]] std::optional<Price> best_ask() const;
    [[nodiscard]] std::optional<Price> mid_price() const;

private:
    uint64_t last_timestamp_nanos_ = 0;
    std::map<uint64_t, BookOrder> orders_by_ref_;
    std::map<uint64_t, Qty> bids_by_price_;
    std::map<uint64_t, Qty> asks_by_price_;
};

} // namespace faircross
