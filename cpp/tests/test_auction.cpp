#include "test_framework.hpp"
#include "faircross/engine/auction.hpp"
#include <limits>

using namespace faircross;

static Order make_order(uint64_t id, Side side, uint64_t price, uint64_t qty, uint64_t seq) {
    return Order{
        OrderId(id),
        AccountId(100 + id),
        InstrumentId(1),
        side,
        Price(price),
        Qty(qty),
        seq
    };
}

TEST_CASE(test_auction_candidate_prices_sorting_and_dedup) {
    std::vector<Order> orders = {
        make_order(1, Side::Buy, 105, 10, 0),
        make_order(2, Side::Sell, 90, 5, 1),
        make_order(3, Side::Buy, 100, 20, 2),
        make_order(4, Side::Sell, 105, 15, 3),
        make_order(5, Side::Sell, 90, 8, 4),
        make_order(6, Side::Buy, 110, 12, 5),
    };

    auto candidates = candidate_prices_from_orders(orders);
    REQUIRE_EQ(candidates.size(), 4);
    REQUIRE_EQ(candidates[0].as_raw(), 90);
    REQUIRE_EQ(candidates[1].as_raw(), 100);
    REQUIRE_EQ(candidates[2].as_raw(), 105);
    REQUIRE_EQ(candidates[3].as_raw(), 110);
}

TEST_CASE(test_auction_tie_breaking_midpoint) {
    // Case 1: Buy 10 @ 105, Sell 10 @ 95 -> maximizers [95, 105] -> midpoint (95+105)/2 = 100
    std::vector<Order> orders1 = {
        make_order(1, Side::Buy, 105, 10, 0),
        make_order(2, Side::Sell, 95, 10, 1),
    };
    Batch batch1(BatchId(1), InstrumentId(1), orders1);
    auto outcome1 = determine_clearing_outcome(batch1).value();
    REQUIRE_EQ(outcome1.executable_volume.as_raw(), 10);
    REQUIRE(outcome1.clearing_price.has_value());
    REQUIRE_EQ(outcome1.clearing_price->as_raw(), 100);

    // Case 2: Three-maximizer plateau -> [90, 100] -> (90+100)/2 = 95
    std::vector<Order> orders2 = {
        make_order(1, Side::Buy, 110, 20, 0),
        make_order(2, Side::Buy, 100, 10, 1),
        make_order(3, Side::Sell, 90, 10, 2),
        make_order(4, Side::Sell, 80, 20, 3),
    };
    Batch batch2(BatchId(1), InstrumentId(1), orders2);
    auto outcome2 = determine_clearing_outcome(batch2).value();
    REQUIRE_EQ(outcome2.executable_volume.as_raw(), 30);
    REQUIRE(outcome2.clearing_price.has_value());
    REQUIRE_EQ(outcome2.clearing_price->as_raw(), 95);

    // Case 3: No cross
    std::vector<Order> orders3 = {
        make_order(1, Side::Buy, 90, 10, 0),
        make_order(2, Side::Sell, 100, 10, 1),
    };
    Batch batch3(BatchId(1), InstrumentId(1), orders3);
    auto outcome3 = determine_clearing_outcome(batch3).value();
    REQUIRE_EQ(outcome3.executable_volume.as_raw(), 0);
    REQUIRE(!outcome3.clearing_price.has_value());

    // Case 4: the endpoint sum overflows uint64_t, but the floor midpoint is
    // still representable: [MAX-2, MAX] -> MAX-1.
    const uint64_t max_price = std::numeric_limits<uint64_t>::max();
    std::vector<Order> orders4 = {
        make_order(1, Side::Buy, max_price, 1, 0),
        make_order(2, Side::Sell, max_price - 2, 1, 1),
    };
    Batch batch4(BatchId(1), InstrumentId(1), orders4);
    auto outcome4 = determine_clearing_outcome(batch4).value();
    REQUIRE_EQ(outcome4.executable_volume.as_raw(), 1);
    REQUIRE(outcome4.clearing_price.has_value());
    REQUIRE_EQ(outcome4.clearing_price->as_raw(), max_price - 1);
}

TEST_CASE(test_batch_create_rejects_invalid_domain_values) {
    const InstrumentId inst(1);

    auto valid = make_order(1, Side::Buy, 100, 10, 0);
    REQUIRE(Batch::create(BatchId(1), inst, {valid}).is_ok());

    auto zero_price = valid;
    zero_price.price = Price(0);
    REQUIRE(Batch::create(BatchId(1), inst, {zero_price}).is_err());

    auto zero_qty = valid;
    zero_qty.qty = Qty::zero();
    REQUIRE(Batch::create(BatchId(1), inst, {zero_qty}).is_err());

    auto invalid_side = valid;
    invalid_side.side = static_cast<Side>(0);
    REQUIRE(Batch::create(BatchId(1), inst, {invalid_side}).is_err());

    auto wrong_instrument = valid;
    wrong_instrument.instrument = InstrumentId(2);
    REQUIRE(Batch::create(BatchId(1), inst, {wrong_instrument}).is_err());

    auto duplicate = valid;
    duplicate.seq = 1;
    REQUIRE(Batch::create(BatchId(1), inst, {valid, duplicate}).is_err());
}
