#include "faircross/simulator/agent.hpp"

namespace faircross {

std::vector<Order> SyntheticMarketMaker::generate_orders(
    MarketEnvironment& env,
    [[maybe_unused]] DeterministicRng& rng
) const {
    uint64_t mid_raw = env.reference_mid.as_raw();
    uint64_t bid_raw = (mid_raw > half_spread_ticks) ? (mid_raw - half_spread_ticks) : 1;
    uint64_t ask_raw = mid_raw + half_spread_ticks;

    Price bid_price(bid_raw);
    Price ask_price(ask_raw);

    OrderId bid_id(env.next_order_id++);
    OrderId ask_id(env.next_order_id++);

    return {
        Order{bid_id, account_id, env.instrument_id, Side::Buy, bid_price, quote_qty, 0},
        Order{ask_id, account_id, env.instrument_id, Side::Sell, ask_price, quote_qty, 1}
    };
}

Order SyntheticNoiseTrader::generate_order(
    MarketEnvironment& env,
    DeterministicRng& rng
) const {
    bool is_buy = rng.gen_bool_permille(buy_probability_permille);
    Side side = is_buy ? Side::Buy : Side::Sell;

    uint64_t mid_raw = env.reference_mid.as_raw();
    uint64_t agg = rng.gen_range_u64(0, max_aggression_ticks);

    uint64_t price_raw = is_buy ? (mid_raw + agg) : ((mid_raw > agg) ? (mid_raw - agg) : 1);
    Price price(price_raw);

    uint64_t qty_raw = rng.gen_range_u64(min_qty.as_raw(), max_qty.as_raw());
    Qty qty(qty_raw);

    OrderId order_id(env.next_order_id++);

    return Order{order_id, account_id, env.instrument_id, side, price, qty, 0};
}

} // namespace faircross
