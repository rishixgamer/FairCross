#pragma once

#include <vector>
#include <optional>
#include "faircross/domain/primitives.hpp"
#include "faircross/domain/order.hpp"

namespace faircross {

struct OrderExecutionMetric {
    uint64_t order_id;
    Side side;
    Qty submitted_qty;
    Qty filled_qty;
    uint64_t arrival_nanos;
    uint64_t batch_cutoff_nanos;
    uint64_t delay_nanos;
    std::optional<Price> arrival_reference_price;
    std::optional<Price> clearing_price;
    std::optional<int64_t> slippage_ticks;
    Money implementation_shortfall;
};

struct MarketQualityReport {
    size_t total_batches;
    size_t total_submitted_orders;
    size_t total_fills;
    uint64_t total_submitted_volume;
    uint64_t total_cleared_volume;
    uint32_t fill_rate_permille;
    uint64_t mean_time_to_fill_nanos;
    uint64_t max_time_to_fill_nanos;
    int64_t mean_slippage_ticks;
    Money total_implementation_shortfall;
    std::vector<OrderExecutionMetric> order_metrics;
};

class MarketQualityCalculator {
public:
    static MarketQualityReport calculate(
        size_t total_batches,
        std::vector<OrderExecutionMetric> order_metrics
    );
};

} // namespace faircross
