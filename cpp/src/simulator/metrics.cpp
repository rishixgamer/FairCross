#include "faircross/simulator/metrics.hpp"
#include <algorithm>

namespace faircross {

MarketQualityReport MarketQualityCalculator::calculate(
    size_t total_batches,
    std::vector<OrderExecutionMetric> order_metrics
) {
    size_t total_submitted_orders = order_metrics.size();
    uint64_t total_submitted_volume = 0;
    for (const auto& m : order_metrics) {
        total_submitted_volume += m.submitted_qty.as_raw();
    }

    size_t total_fills = 0;
    uint64_t total_filled_lots = 0;
    uint64_t total_delay = 0;
    uint64_t max_delay = 0;
    int64_t total_slippage = 0;
    size_t slippage_count = 0;
    unsigned __int128 total_shortfall_raw = 0;

    for (const auto& m : order_metrics) {
        if (!m.filled_qty.is_zero()) {
            total_fills++;
            total_filled_lots += m.filled_qty.as_raw();
            total_delay += m.delay_nanos;
            max_delay = std::max(max_delay, m.delay_nanos);

            if (m.slippage_ticks.has_value()) {
                total_slippage += m.slippage_ticks.value();
                slippage_count++;
            }

            total_shortfall_raw += m.implementation_shortfall.as_raw();
        }
    }

    uint64_t total_cleared_volume = total_filled_lots / 2;
    uint32_t fill_rate_permille = 0;
    if (total_submitted_volume > 0) {
        fill_rate_permille = static_cast<uint32_t>((static_cast<unsigned __int128>(total_filled_lots) * 1000) / total_submitted_volume);
    }

    uint64_t mean_time_to_fill_nanos = (total_fills > 0) ? (total_delay / total_fills) : 0;
    int64_t mean_slippage_ticks = (slippage_count > 0) ? (total_slippage / static_cast<int64_t>(slippage_count)) : 0;

    return MarketQualityReport{
        total_batches,
        total_submitted_orders,
        total_fills,
        total_submitted_volume,
        total_cleared_volume,
        fill_rate_permille,
        mean_time_to_fill_nanos,
        max_delay,
        mean_slippage_ticks,
        Money::from_raw(total_shortfall_raw),
        std::move(order_metrics)
    };
}

} // namespace faircross
