#pragma once

#include "faircross/domain/primitives.hpp"
#include "faircross/domain/order.hpp"

namespace faircross {

enum class OrderMoneyness {
    InTheMoney,
    AtTheMoney,
    OutOfTheMoney,
};

inline OrderMoneyness classify_order(const Order& order, Price clearing_price) {
    if (order.side == Side::Buy) {
        if (order.price.as_raw() > clearing_price.as_raw()) {
            return OrderMoneyness::InTheMoney;
        } else if (order.price.as_raw() == clearing_price.as_raw()) {
            return OrderMoneyness::AtTheMoney;
        } else {
            return OrderMoneyness::OutOfTheMoney;
        }
    } else {
        if (order.price.as_raw() < clearing_price.as_raw()) {
            return OrderMoneyness::InTheMoney;
        } else if (order.price.as_raw() == clearing_price.as_raw()) {
            return OrderMoneyness::AtTheMoney;
        } else {
            return OrderMoneyness::OutOfTheMoney;
        }
    }
}

} // namespace faircross
