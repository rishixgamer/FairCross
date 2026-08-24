#include "faircross/marketdata/book.hpp"
#include <type_traits>
#include <variant>

namespace faircross {

Result<Ok> OrderBook::add_order(uint64_t ref_num, Side side, Price price, Qty shares) {
    if (!is_buy(side) && !is_sell(side)) {
        return PrimitiveError{PrimitiveErrorKind::InvalidSide,
                              "OrderBook::add_order"};
    }
    if (price.as_raw() == 0) {
        return PrimitiveError{PrimitiveErrorKind::ZeroValue, "Price"};
    }
    if (shares.is_zero()) {
        return PrimitiveError{PrimitiveErrorKind::ZeroValue, "Qty"};
    }
    if (orders_by_ref_.find(ref_num) != orders_by_ref_.end()) {
        return DomainError{DomainErrorKind::DuplicateOrderId, "Order already in book"};
    }

    uint64_t p_raw = price.as_raw();
    auto& levels = (side == Side::Buy) ? bids_by_price_ : asks_by_price_;
    auto level_it = levels.find(p_raw);
    Qty current_level = (level_it == levels.end()) ? Qty::zero() : level_it->second;
    auto total_res = current_level.checked_add(shares);
    if (total_res.is_err()) return total_res;

    // Commit only after the aggregate arithmetic has succeeded.  In
    // particular, an overflowing level must not leave a phantom order in the
    // reference map or throw while unwrapping a failed Result.
    orders_by_ref_.emplace(ref_num, BookOrder{ref_num, side, price, shares});
    levels[p_raw] = total_res.value();

    return ok;
}

Result<Ok> OrderBook::execute_order(uint64_t ref_num, Qty shares) {
    auto it = orders_by_ref_.find(ref_num);
    if (it == orders_by_ref_.end()) {
        return DomainError{DomainErrorKind::AccountNotFound, "Order not in book"};
    }

    BookOrder& order = it->second;
    uint64_t p_raw = order.price.as_raw();

    if (shares.is_zero()) {
        return PrimitiveError{PrimitiveErrorKind::ZeroValue, "Qty"};
    }
    if (shares.as_raw() > order.shares.as_raw()) {
        return DomainError{DomainErrorKind::OrderValidationFailed,
                           "Executed quantity exceeds resting order"};
    }

    if (shares == order.shares) {
        if (order.side == Side::Buy) {
            bids_by_price_[p_raw] = bids_by_price_[p_raw].saturating_sub(order.shares);
            if (bids_by_price_[p_raw].is_zero()) bids_by_price_.erase(p_raw);
        } else {
            asks_by_price_[p_raw] = asks_by_price_[p_raw].saturating_sub(order.shares);
            if (asks_by_price_[p_raw].is_zero()) asks_by_price_.erase(p_raw);
        }
        orders_by_ref_.erase(it);
    } else {
        order.shares = order.shares.saturating_sub(shares);
        if (order.side == Side::Buy) {
            bids_by_price_[p_raw] = bids_by_price_[p_raw].saturating_sub(shares);
        } else {
            asks_by_price_[p_raw] = asks_by_price_[p_raw].saturating_sub(shares);
        }
    }

    return ok;
}

Result<Ok> OrderBook::cancel_order(uint64_t ref_num, Qty shares) {
    return execute_order(ref_num, shares);
}

Result<Ok> OrderBook::delete_order(uint64_t ref_num) {
    auto it = orders_by_ref_.find(ref_num);
    if (it == orders_by_ref_.end()) {
        return DomainError{DomainErrorKind::AccountNotFound, "Order not in book"};
    }
    return execute_order(ref_num, it->second.shares);
}

Result<Ok> OrderBook::replace_order(uint64_t orig_ref, uint64_t new_ref, Qty new_shares, Price new_price) {
    auto it = orders_by_ref_.find(orig_ref);
    if (it == orders_by_ref_.end()) {
        return DomainError{DomainErrorKind::AccountNotFound, "Original order not in book"};
    }

    if (new_ref != orig_ref && orders_by_ref_.find(new_ref) != orders_by_ref_.end()) {
        return DomainError{DomainErrorKind::DuplicateOrderId,
                           "Replacement order already in book"};
    }
    if (new_price.as_raw() == 0) {
        return PrimitiveError{PrimitiveErrorKind::ZeroValue, "Price"};
    }
    if (new_shares.is_zero()) {
        return PrimitiveError{PrimitiveErrorKind::ZeroValue, "Qty"};
    }

    Side side = it->second.side;
    Price old_price = it->second.price;
    Qty old_shares = it->second.shares;
    auto& levels = (side == Side::Buy) ? bids_by_price_ : asks_by_price_;
    const uint64_t old_raw = old_price.as_raw();
    const uint64_t new_raw = new_price.as_raw();

    auto old_level_it = levels.find(old_raw);
    if (old_level_it == levels.end()) {
        return DomainError{DomainErrorKind::OrderValidationFailed,
                           "Order book index inconsistency"};
    }
    auto old_remaining_res = old_level_it->second.checked_sub(old_shares);
    if (old_remaining_res.is_err()) return old_remaining_res;

    Qty new_level_base = Qty::zero();
    if (new_raw == old_raw) {
        new_level_base = old_remaining_res.value();
    } else {
        auto new_level_it = levels.find(new_raw);
        if (new_level_it != levels.end()) new_level_base = new_level_it->second;
    }
    auto new_total_res = new_level_base.checked_add(new_shares);
    if (new_total_res.is_err()) return new_total_res;

    // All validation and arithmetic completed before either index is changed.
    // Commit the replacement as one logical update.
    orders_by_ref_.erase(it);
    if (new_raw != old_raw) {
        if (old_remaining_res.value().is_zero()) {
            levels.erase(old_raw);
        } else {
            levels[old_raw] = old_remaining_res.value();
        }
    }
    levels[new_raw] = new_total_res.value();
    orders_by_ref_.emplace(new_ref,
                           BookOrder{new_ref, side, new_price, new_shares});

    return ok;
}

std::optional<Price> OrderBook::best_bid() const {
    if (bids_by_price_.empty()) return std::nullopt;
    return Price(bids_by_price_.rbegin()->first);
}

std::optional<Price> OrderBook::best_ask() const {
    if (asks_by_price_.empty()) return std::nullopt;
    return Price(asks_by_price_.begin()->first);
}

std::optional<Price> OrderBook::mid_price() const {
    auto bb = best_bid();
    auto ba = best_ask();
    if (bb.has_value() && ba.has_value()) {
        uint64_t bid_raw = bb->as_raw();
        uint64_t ask_raw = ba->as_raw();
        uint64_t low = (bid_raw < ask_raw) ? bid_raw : ask_raw;
        uint64_t high = (bid_raw < ask_raw) ? ask_raw : bid_raw;
        uint64_t mid = low + (high - low) / 2;
        auto res = Price::create(mid);
        if (res.is_ok()) return res.value();
    }
    return std::nullopt;
}

Result<Ok> OrderBook::apply_message(const ItchMessage& msg) {
    return std::visit(
        [this](const auto& m) -> Result<Ok> {
            using T = std::decay_t<decltype(m)>;
            last_timestamp_nanos_ = m.timestamp_nanos;

            if constexpr (std::is_same_v<T, AddOrderMessage>) {
                return add_order(m.order_reference_number, m.side, Price(m.price_ticks),
                                 Qty(m.shares));
            } else if constexpr (std::is_same_v<T, OrderExecutedMessage>) {
                return cancel_order(m.order_reference_number, Qty(m.executed_shares));
            } else if constexpr (std::is_same_v<T, OrderExecutedWithPriceMessage>) {
                return cancel_order(m.order_reference_number, Qty(m.executed_shares));
            } else if constexpr (std::is_same_v<T, OrderCancelMessage>) {
                return cancel_order(m.order_reference_number, Qty(m.canceled_shares));
            } else if constexpr (std::is_same_v<T, OrderDeleteMessage>) {
                return delete_order(m.order_reference_number);
            } else if constexpr (std::is_same_v<T, OrderReplaceMessage>) {
                return replace_order(m.original_order_reference_number,
                                     m.new_order_reference_number,
                                     Qty(m.shares),
                                     Price(m.price_ticks));
            } else {
                // SystemEvent and StockDirectory advance the clock only.
                return ok;
            }
        },
        msg);
}

} // namespace faircross
