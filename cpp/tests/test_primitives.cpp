#include "test_framework.hpp"
#include "faircross/domain/primitives.hpp"

using namespace faircross;

TEST_CASE(test_price_validation_and_overflow) {
    auto p0 = Price::create(0);
    REQUIRE(p0.is_err());

    auto p100 = Price::create(100);
    REQUIRE(p100.is_ok());
    REQUIRE_EQ(p100.value().as_raw(), 100);

    Price p_max(Price::MAX_RAW);
    Price p_one(1);
    auto overflow_res = p_max.checked_add(p_one);
    REQUIRE(overflow_res.is_err());

    auto underflow_res = p_one.checked_sub(p_max);
    REQUIRE(underflow_res.is_err());
}

TEST_CASE(test_qty_validation_and_operations) {
    auto q0 = Qty::create(0);
    REQUIRE(q0.is_err());

    auto q50 = Qty::create(50);
    REQUIRE(q50.is_ok());
    REQUIRE_EQ(q50.value().as_raw(), 50);

    Qty q_max(Qty::MAX_RAW);
    REQUIRE(q_max.checked_add(Qty(1)).is_err());
    REQUIRE(Qty(10).checked_sub(Qty(20)).is_err());
    REQUIRE_EQ(Qty(10).saturating_sub(Qty(20)).as_raw(), 0);
}

TEST_CASE(test_money_math_and_overflow) {
    Money m1 = Money::from_raw(1000);
    Money m2 = Money::from_raw(500);
    REQUIRE_EQ(m1.checked_add(m2).value().as_raw(), 1500);
    REQUIRE_EQ(m1.checked_sub(m2).value().as_raw(), 500);
    REQUIRE(m2.checked_sub(m1).is_err());

    Price p(100);
    Qty q(25);
    auto cons = Money::from_price_qty(p, q);
    REQUIRE(cons.is_ok());
    REQUIRE_EQ(cons.value().as_raw(), 2500);
}
