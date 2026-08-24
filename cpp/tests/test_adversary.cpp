#include "test_framework.hpp"
#include "faircross/adversary/matrix.hpp"

using namespace faircross;

TEST_CASE(test_adversary_matrix_all_attacks_rejected) {
    InstrumentId inst(1);
    Ledger ledger;
    ledger.insert_account(AccountState(AccountId(1), Money::from_raw(10000)));
    AccountState a2(AccountId(2), Money::zero());
    auto _ = a2.credit_inventory(inst, Qty(50));
    ledger.insert_account(std::move(a2));

    std::vector<Order> orders = {
        Order{OrderId(1), AccountId(1), inst, Side::Buy, Price(100), Qty(20), 0},
        Order{OrderId(2), AccountId(2), inst, Side::Sell, Price(100), Qty(20), 1},
    };
    std::vector<SaltedOrderPreimage> preimages = {
        SaltedOrderPreimage(orders[0], std::array<uint8_t, 32>{1}),
        SaltedOrderPreimage(orders[1], std::array<uint8_t, 32>{2}),
    };
    Batch batch(BatchId(1), inst, orders);

    auto report = run_attack_matrix(ledger, batch, preimages);

    for (const auto& r : report.results) {
        if (!r.expected_behavior_met) {
            std::cerr << "FAIL: " << r.attack_name << " (expected_pass=" << r.is_honest_control 
                      << ", actual_pass=" << r.passed_verification << ")\n";
        }
    }

    REQUIRE(report.all_invariants_held);
    REQUIRE_EQ(report.total_evaluated, 12);
    REQUIRE_EQ(report.total_attacks_rejected, 11);
    REQUIRE(report.results[0].is_honest_control);
    REQUIRE(report.results[0].passed_verification);
}
