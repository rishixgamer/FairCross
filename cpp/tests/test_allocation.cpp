#include "test_framework.hpp"
#include "faircross/engine/allocation.hpp"

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

TEST_CASE(test_pro_rata_largest_remainder_allocation) {
    // 3 ATM buyers: 10 lots each. Target = 10.
    // Base fill = 10 * 10 / 30 = 3 remainder 10.
    // Sum base = 9. Surplus = 1. B1 (seq 0) gets extra 1 lot -> 4, 3, 3.
    std::vector<Order> orders = {
        make_order(1, Side::Buy, 100, 10, 0),
        make_order(2, Side::Buy, 100, 10, 1),
        make_order(3, Side::Buy, 100, 10, 2),
    };

    auto allocs = allocate_side(orders, Price(100), Qty(10), Side::Buy).value();
    REQUIRE_EQ(allocs.size(), 3);
    REQUIRE_EQ(allocs[0].allocated_qty.as_raw(), 4);
    REQUIRE_EQ(allocs[1].allocated_qty.as_raw(), 3);
    REQUIRE_EQ(allocs[2].allocated_qty.as_raw(), 3);
}

TEST_CASE(test_price_priority_plus_pro_rata) {
    // B1: Buy 10 @ 105 (ITM) -> 10
    // B2: Buy 20 @ 100 (ATM) -> 10
    // B3: Buy 10 @ 100 (ATM) -> 5
    // B4: Buy 10 @ 95 (OTM) -> 0
    std::vector<Order> orders = {
        make_order(1, Side::Buy, 105, 10, 0),
        make_order(2, Side::Buy, 100, 20, 1),
        make_order(3, Side::Buy, 100, 10, 2),
        make_order(4, Side::Buy, 95, 10, 3),
    };

    auto allocs = allocate_side(orders, Price(100), Qty(25), Side::Buy).value();
    REQUIRE_EQ(allocs[0].allocated_qty.as_raw(), 10);
    REQUIRE_EQ(allocs[1].allocated_qty.as_raw(), 10);
    REQUIRE_EQ(allocs[2].allocated_qty.as_raw(), 5);
    REQUIRE_EQ(allocs[3].allocated_qty.as_raw(), 0);
}
