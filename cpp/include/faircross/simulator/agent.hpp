#pragma once

#include <vector>
#include <optional>
#include "faircross/domain/primitives.hpp"
#include "faircross/domain/order.hpp"
#include "faircross/simulator/rng.hpp"

namespace faircross {

struct MarketEnvironment {
    InstrumentId instrument_id;
    Price reference_mid;
    std::optional<Price> last_clearing_price;
    uint64_t next_order_id;
};

class SyntheticMarketMaker {
public:
    AccountId account_id;
    uint64_t half_spread_ticks;
    Qty quote_qty;

    SyntheticMarketMaker(AccountId acc, uint64_t half_spread, Qty qty)
        : account_id(acc), half_spread_ticks(half_spread), quote_qty(qty) {}

    std::vector<Order> generate_orders(MarketEnvironment& env, DeterministicRng& rng) const;
};

class SyntheticNoiseTrader {
public:
    AccountId account_id;
    uint32_t buy_probability_permille;
    uint64_t max_aggression_ticks;
    Qty min_qty;
    Qty max_qty;

    SyntheticNoiseTrader(AccountId acc, uint32_t buy_prob, uint64_t max_agg, Qty min_q, Qty max_q)
        : account_id(acc), buy_probability_permille(buy_prob), max_aggression_ticks(max_agg),
          min_qty(min_q), max_qty(max_q) {}

    Order generate_order(MarketEnvironment& env, DeterministicRng& rng) const;
};

} // namespace faircross
