#include "test_framework.hpp"
#include "faircross/engine/checker.hpp"

using namespace faircross;

static Order make_order(uint64_t id, uint64_t acc, Side side, uint64_t price, uint64_t qty, uint64_t seq) {
    return Order{
        OrderId(id),
        AccountId(acc),
        InstrumentId(1),
        side,
        Price(price),
        Qty(qty),
        seq
    };
}

TEST_CASE(test_checker_accepts_honest_and_rejects_mutations) {
    InstrumentId inst(1);
    Ledger pre_state;
    pre_state.insert_account(AccountState(AccountId(1), Money::from_raw(10000)));
    AccountState a2(AccountId(2), Money::zero());
    auto _ = a2.credit_inventory(inst, Qty(50));
    pre_state.insert_account(std::move(a2));

    std::vector<Order> orders = {
        make_order(1, 1, Side::Buy, 100, 20, 0),
        make_order(2, 2, Side::Sell, 100, 20, 1),
    };
    Batch batch(BatchId(1), inst, orders);
    auto honest_res = execute_batch(pre_state, batch).value();

    // 1. Honest transition passes
    REQUIRE(verify_transition(pre_state, batch, honest_res).is_ok());

    // 2. Cash creation fails
    auto bad_cash_res = honest_res;
    auto __ = bad_cash_res.post_state.get_or_create_account(AccountId(1)).credit_cash(Money::from_raw(500));
    REQUIRE(verify_transition(pre_state, batch, bad_cash_res).is_err());

    // 3. Asset creation fails
    auto bad_asset_res = honest_res;
    auto ___ = bad_asset_res.post_state.get_or_create_account(AccountId(2)).credit_inventory(inst, Qty(5));
    REQUIRE(verify_transition(pre_state, batch, bad_asset_res).is_err());
}

TEST_CASE(test_checker_rejects_noncanonical_allocation_and_fills) {
    InstrumentId inst(1);
    Ledger pre_state;
    pre_state.insert_account(AccountState(AccountId(1), Money::from_raw(10000)));
    AccountState seller(AccountId(2), Money::zero());
    REQUIRE(seller.credit_inventory(inst, Qty(50)).is_ok());
    pre_state.insert_account(std::move(seller));

    // These spare accounts make the historical account-redirection forgery
    // executable: redirected fills can settle without violating aggregates.
    pre_state.insert_account(AccountState(AccountId(3), Money::from_raw(10000)));
    AccountState spare_seller(AccountId(4), Money::zero());
    REQUIRE(spare_seller.credit_inventory(inst, Qty(50)).is_ok());
    pre_state.insert_account(std::move(spare_seller));

    std::vector<Order> orders = {
        make_order(1, 1, Side::Buy, 100, 20, 0),
        make_order(2, 2, Side::Sell, 100, 20, 1),
    };
    Batch batch(BatchId(1), inst, orders);
    auto honest_res = execute_batch(pre_state, batch).value();
    REQUIRE(verify_transition(pre_state, batch, honest_res).is_ok());

    // The old checker validated only allocation quantities.  A forged
    // account in the allocation must now fail full-structure comparison.
    auto bad_allocation = honest_res;
    bad_allocation.allocation.allocations.front().account = AccountId(3);
    REQUIRE(verify_transition(pre_state, batch, bad_allocation).is_err());

    // Redirect both fills and rebuild the corresponding post-state so that
    // cash, inventory, and per-account reconstruction all remain consistent.
    // Aggregate-only checking accepted this historical forgery.
    auto redirected = honest_res;
    redirected.fills[0].account_id = AccountId(3);
    redirected.fills[1].account_id = AccountId(4);
    auto redirected_post = apply_batch_transition(pre_state, redirected.fills);
    REQUIRE(redirected_post.is_ok());
    redirected.post_state = redirected_post.value();
    REQUIRE(verify_transition(pre_state, batch, redirected).is_err());

    // Every fill field is canonical, not merely its quantity.  Mutating any
    // of these fields must be rejected before post-state reconstruction.
    auto require_fill_mutation_rejected = [&](const auto& mutate) {
        auto candidate = honest_res;
        mutate(candidate.fills.front());
        REQUIRE(verify_transition(pre_state, batch, candidate).is_err());
    };
    require_fill_mutation_rejected([](Fill& fill) { fill.fill_id = 99; });
    require_fill_mutation_rejected([](Fill& fill) { fill.order_id = OrderId(2); });
    require_fill_mutation_rejected([](Fill& fill) { fill.account_id = AccountId(3); });
    require_fill_mutation_rejected([](Fill& fill) { fill.instrument_id = InstrumentId(2); });
    require_fill_mutation_rejected([](Fill& fill) { fill.side = Side::Sell; });
    require_fill_mutation_rejected([](Fill& fill) { fill.execution_price = Price(99); });
    require_fill_mutation_rejected([](Fill& fill) { fill.fill_qty = Qty::from_raw(19); });
    require_fill_mutation_rejected([](Fill& fill) { fill.consideration = Money::from_raw(1); });

    auto extra_fill = honest_res;
    extra_fill.fills.push_back(honest_res.fills.front());
    REQUIRE(verify_transition(pre_state, batch, extra_fill).is_err());

    auto missing_fill = honest_res;
    missing_fill.fills.erase(missing_fill.fills.begin());
    REQUIRE(verify_transition(pre_state, batch, missing_fill).is_err());
}
