#pragma once

#include <cstdint>
#include <array>
#include <optional>
#include <variant>
#include "faircross/domain/order.hpp"

namespace faircross {

struct SystemEventMessage {
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint64_t timestamp_nanos;
    char event_code;
};

struct StockDirectoryMessage {
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint64_t timestamp_nanos;
    std::array<uint8_t, 8> symbol;
    uint32_t round_lot_size;
};

struct AddOrderMessage {
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint64_t timestamp_nanos;
    uint64_t order_reference_number;
    Side side;
    uint32_t shares;
    std::array<uint8_t, 8> symbol;
    uint32_t price_ticks;
    std::optional<std::array<uint8_t, 4>> attribution;
};

struct OrderExecutedMessage {
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint64_t timestamp_nanos;
    uint64_t order_reference_number;
    uint32_t executed_shares;
    uint64_t match_number;
};

struct OrderExecutedWithPriceMessage {
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint64_t timestamp_nanos;
    uint64_t order_reference_number;
    uint32_t executed_shares;
    uint64_t match_number;
    bool printable;
    uint32_t execution_price_ticks;
};

struct OrderCancelMessage {
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint64_t timestamp_nanos;
    uint64_t order_reference_number;
    uint32_t canceled_shares;
};

struct OrderDeleteMessage {
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint64_t timestamp_nanos;
    uint64_t order_reference_number;
};

struct OrderReplaceMessage {
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint64_t timestamp_nanos;
    uint64_t original_order_reference_number;
    uint64_t new_order_reference_number;
    uint32_t shares;
    uint32_t price_ticks;
};

using ItchMessage = std::variant<
    SystemEventMessage,
    StockDirectoryMessage,
    AddOrderMessage,
    OrderExecutedMessage,
    OrderExecutedWithPriceMessage,
    OrderCancelMessage,
    OrderDeleteMessage,
    OrderReplaceMessage
>;

} // namespace faircross
