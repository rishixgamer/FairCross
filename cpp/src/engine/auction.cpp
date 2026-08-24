#include "faircross/engine/auction.hpp"
#include <algorithm>

namespace faircross {

Result<Qty> demand_from_orders(const std::vector<Order>& orders, Price price) {
    Qty total = Qty::zero();
    for (const auto& order : orders) {
        if (order.side == Side::Buy && order.price.as_raw() >= price.as_raw()) {
            auto res = total.checked_add(order.qty);
            if (res.is_err()) return res;
            total = res.value();
        }
    }
    return total;
}

Result<Qty> supply_from_orders(const std::vector<Order>& orders, Price price) {
    Qty total = Qty::zero();
    for (const auto& order : orders) {
        if (order.side == Side::Sell && order.price.as_raw() <= price.as_raw()) {
            auto res = total.checked_add(order.qty);
            if (res.is_err()) return res;
            total = res.value();
        }
    }
    return total;
}

std::vector<Price> candidate_prices_from_orders(const std::vector<Order>& orders) {
    if (orders.empty()) return {};

    std::vector<Price> prices;
    prices.reserve(orders.size());
    for (const auto& o : orders) {
        prices.push_back(o.price);
    }
    std::sort(prices.begin(), prices.end());
    prices.erase(std::unique(prices.begin(), prices.end()), prices.end());
    return prices;
}

Result<VolumeMaximizers> compute_volume_maximizers_from_orders(const std::vector<Order>& orders) {
    auto candidates = candidate_prices_from_orders(orders);
    size_t num_candidates = candidates.size();
    if (num_candidates == 0) {
        return VolumeMaximizers{Qty::zero(), {}, {}};
    }

    std::vector<Qty> buy_qty_at_price(num_candidates, Qty::zero());
    std::vector<Qty> sell_qty_at_price(num_candidates, Qty::zero());

    for (const auto& order : orders) {
        auto it = std::lower_bound(candidates.begin(), candidates.end(), order.price);
        if (it != candidates.end() && *it == order.price) {
            size_t idx = static_cast<size_t>(std::distance(candidates.begin(), it));
            if (order.side == Side::Buy) {
                auto res = buy_qty_at_price[idx].checked_add(order.qty);
                if (res.is_err()) return res;
                buy_qty_at_price[idx] = res.value();
            } else {
                auto res = sell_qty_at_price[idx].checked_add(order.qty);
                if (res.is_err()) return res;
                sell_qty_at_price[idx] = res.value();
            }
        }
    }

    std::vector<Qty> supply_at_cand(num_candidates, Qty::zero());
    Qty running_supply = Qty::zero();
    for (size_t k = 0; k < num_candidates; ++k) {
        auto res = running_supply.checked_add(sell_qty_at_price[k]);
        if (res.is_err()) return res;
        running_supply = res.value();
        supply_at_cand[k] = running_supply;
    }

    std::vector<Qty> demand_at_cand(num_candidates, Qty::zero());
    Qty running_demand = Qty::zero();
    for (size_t i = 0; i < num_candidates; ++i) {
        size_t k = num_candidates - 1 - i;
        auto res = running_demand.checked_add(buy_qty_at_price[k]);
        if (res.is_err()) return res;
        running_demand = res.value();
        demand_at_cand[k] = running_demand;
    }

    std::vector<CandidateEvaluation> evaluations;
    evaluations.reserve(num_candidates);
    Qty max_volume = Qty::zero();

    for (size_t k = 0; k < num_candidates; ++k) {
        Price price = candidates[k];
        Qty d = demand_at_cand[k];
        Qty s = supply_at_cand[k];
        Qty volume = (d.as_raw() < s.as_raw()) ? d : s;

        if (volume.as_raw() > max_volume.as_raw()) {
            max_volume = volume;
        }

        evaluations.push_back(CandidateEvaluation{price, d, s, volume});
    }

    std::vector<Price> maximizing_prices;
    for (const auto& eval : evaluations) {
        if (eval.executable_volume == max_volume) {
            maximizing_prices.push_back(eval.price);
        }
    }

    return VolumeMaximizers{max_volume, std::move(evaluations), std::move(maximizing_prices)};
}

std::optional<Price> resolve_clearing_price_tie(const VolumeMaximizers& maximizers) {
    if (maximizers.max_volume.is_zero() || maximizers.maximizing_prices.empty()) {
        return std::nullopt;
    }

    uint64_t p_min = maximizers.maximizing_prices.front().as_raw();
    uint64_t p_max = maximizers.maximizing_prices.back().as_raw();
    // Compute floor((p_min + p_max) / 2) without adding the endpoints:
    // prices are uint64_t and their sum can overflow even though the
    // midpoint is a valid Price.
    uint64_t mid = p_min + (p_max - p_min) / 2;

    auto res = Price::create(mid);
    if (res.is_ok()) return res.value();
    return std::nullopt;
}

Result<ClearingOutcome> determine_clearing_outcome(const Batch& batch) {
    auto max_res = compute_volume_maximizers_from_orders(batch.orders());
    if (max_res.is_err()) return max_res;

    const auto& maximizers = max_res.value();
    auto clearing_price = resolve_clearing_price_tie(maximizers);
    return ClearingOutcome{clearing_price, maximizers.max_volume};
}

} // namespace faircross
