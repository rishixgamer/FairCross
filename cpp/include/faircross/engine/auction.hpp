#pragma once

#include <vector>
#include <optional>
#include "faircross/domain/primitives.hpp"
#include "faircross/domain/order.hpp"
#include "faircross/engine/batch.hpp"

namespace faircross {

struct CandidateEvaluation {
    Price price;
    Qty demand;
    Qty supply;
    Qty executable_volume;

    auto operator<=>(const CandidateEvaluation&) const = default;
};

struct VolumeMaximizers {
    Qty max_volume;
    std::vector<CandidateEvaluation> evaluations;
    std::vector<Price> maximizing_prices;

    auto operator<=>(const VolumeMaximizers&) const = default;
};

struct ClearingOutcome {
    std::optional<Price> clearing_price;
    Qty executable_volume;

    auto operator<=>(const ClearingOutcome&) const = default;
};

Result<Qty> demand_from_orders(const std::vector<Order>& orders, Price price);
Result<Qty> supply_from_orders(const std::vector<Order>& orders, Price price);
std::vector<Price> candidate_prices_from_orders(const std::vector<Order>& orders);
Result<VolumeMaximizers> compute_volume_maximizers_from_orders(const std::vector<Order>& orders);
std::optional<Price> resolve_clearing_price_tie(const VolumeMaximizers& maximizers);

Result<ClearingOutcome> determine_clearing_outcome(const Batch& batch);

} // namespace faircross
