#pragma once

// Configuration for synthetic order-flow simulation.
//
// Labelled *synthetic* deliberately: this generator produces stylized
// pseudo-random order flow for invariant testing and benchmark workloads. It is
// not a behavioural market model and results from it must not be presented as
// realistic microstructure.

#include <cstdint>
#include "faircross/domain/primitives.hpp"

namespace faircross {

struct SyntheticFlowConfig {
    uint64_t seed = 42;
    InstrumentId instrument_id = InstrumentId(1);
    size_t num_accounts = 10;
    Money initial_cash_per_account = Money::from_raw(1'000'000);
    Qty initial_inventory_per_account = Qty(10'000);
    size_t num_batches = 5;
    size_t min_orders_per_batch = 4;
    size_t max_orders_per_batch = 10;
    Price price_mid = Price(100);
    uint64_t price_spread_ticks = 5;
    Qty min_order_qty = Qty(1);
    Qty max_order_qty = Qty(20);
    uint32_t buy_probability_permille = 500;

    auto operator<=>(const SyntheticFlowConfig&) const = default;
};

} // namespace faircross
