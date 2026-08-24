#include "faircross/engine/checker.hpp"
#include "faircross/engine/transition.hpp"
#include <string>
#include <map>

namespace faircross {

Result<Ok> verify_transition(
    const Ledger& pre_state,
    const Batch& batch,
    const BatchExecutionResult& proposed
) {
    // 1. Verify Clearing Outcome
    auto expected_outcome_res = determine_clearing_outcome(batch);
    if (expected_outcome_res.is_err()) {
        return DomainError{DomainErrorKind::OrderValidationFailed, "NonDeterministicClearing: failed to compute outcome"};
    }
    ClearingOutcome expected_outcome = expected_outcome_res.value();

    if (expected_outcome != proposed.clearing_outcome) {
        return DomainError{DomainErrorKind::OrderValidationFailed, "NonDeterministicClearing: outcome mismatch"};
    }

    // 2. Verify Allocation
    auto expected_alloc_res = allocate_batch(batch, expected_outcome.clearing_price, expected_outcome.executable_volume);
    if (expected_alloc_res.is_err()) {
        return DomainError{DomainErrorKind::OrderValidationFailed, "AllocationRuleMismatch: failed to compute allocation"};
    }
    const auto& expected_alloc = expected_alloc_res.value();

    if (expected_alloc != proposed.allocation) {
        return DomainError{DomainErrorKind::OrderValidationFailed,
                           "AllocationRuleMismatch: canonical allocation mismatch"};
    }

    // Fills are a canonical serialization of the allocation.  Comparing the
    // complete vector (rather than validating only quantities) binds every
    // externally visible field: IDs, order/account/instrument/side, price,
    // quantity, consideration, and vector order.
    auto expected_fills_res = generate_canonical_fills(batch, expected_alloc);
    if (expected_fills_res.is_err()) {
        return DomainError{DomainErrorKind::OrderValidationFailed,
                           "AllocationRuleMismatch: failed to generate canonical fills"};
    }
    if (expected_fills_res.value() != proposed.fills) {
        return DomainError{DomainErrorKind::OrderValidationFailed,
                           "AllocationRuleMismatch: canonical fills mismatch"};
    }

    // 3. Verify Fills
    std::map<uint64_t, const Order*> order_map;
    for (const auto& o : batch.orders()) {
        order_map[o.id.as_raw()] = &o;
    }

    Qty total_buy_qty = Qty::zero();
    Qty total_sell_qty = Qty::zero();

    for (const auto& fill : proposed.fills) {
        auto it = order_map.find(fill.order_id.as_raw());
        if (it == order_map.end()) {
            return DomainError{DomainErrorKind::OrderValidationFailed, "AccountStateMismatch: fill references order not in batch"};
        }
        const Order* order = it->second;

        // Check overfill
        if (fill.fill_qty.as_raw() > order->qty.as_raw()) {
            return DomainError{DomainErrorKind::OrderValidationFailed, "Overfill: fill_qty > order_qty"};
        }

        // Check limit price
        if (fill.side == Side::Buy) {
            if (fill.execution_price.as_raw() > order->price.as_raw()) {
                return DomainError{DomainErrorKind::OrderValidationFailed, "LimitPriceViolation: buy fill above limit"};
            }
            auto res = total_buy_qty.checked_add(fill.fill_qty);
            if (res.is_err()) return res;
            total_buy_qty = res.value();
        } else {
            if (fill.execution_price.as_raw() < order->price.as_raw()) {
                return DomainError{DomainErrorKind::OrderValidationFailed, "LimitPriceViolation: sell fill below limit"};
            }
            auto res = total_sell_qty.checked_add(fill.fill_qty);
            if (res.is_err()) return res;
            total_sell_qty = res.value();
        }

        // Check consideration
        auto exp_cons_res = Money::from_price_qty(fill.execution_price, fill.fill_qty);
        if (exp_cons_res.is_err() || fill.consideration != exp_cons_res.value()) {
            return DomainError{DomainErrorKind::OrderValidationFailed, "IncorrectConsideration"};
        }
    }

    // 4. Verify Volume Conservation
    if (total_buy_qty != total_sell_qty) {
        return DomainError{DomainErrorKind::OrderValidationFailed, "VolumeConservation: buy != sell"};
    }

    // 5. Verify Cash Conservation
    auto pre_cash_res = pre_state.total_cash();
    auto post_cash_res = proposed.post_state.total_cash();
    if (pre_cash_res.is_err() || post_cash_res.is_err() || pre_cash_res.value() != post_cash_res.value()) {
        return DomainError{DomainErrorKind::OrderValidationFailed, "CashConservation: pre_cash != post_cash"};
    }

    // 6. Verify Asset Conservation
    InstrumentId inst = batch.instrument();
    auto pre_inv_res = pre_state.total_inventory_of(inst);
    auto post_inv_res = proposed.post_state.total_inventory_of(inst);
    if (pre_inv_res.is_err() || post_inv_res.is_err() || pre_inv_res.value() != post_inv_res.value()) {
        return DomainError{DomainErrorKind::OrderValidationFailed, "AssetConservation: pre_inv != post_inv"};
    }

    // 7. Verify per-account post-state reconstruction.
    //
    // Steps 5 and 6 check only aggregates, and a post-state identical to the
    // pre-state satisfies both trivially: an operator could publish real fills
    // while no participant's balance moved, and the totals would still
    // reconcile. Rebuild the ledger those fills imply and require the proposed
    // post-state to match it account by account.
    auto expected_post_res = apply_batch_transition(pre_state, proposed.fills);
    if (expected_post_res.is_err()) {
        return DomainError{DomainErrorKind::OrderValidationFailed,
                           "AccountStateMismatch: proposed fills do not apply cleanly to the pre-state"};
    }
    const Ledger& expected_post = expected_post_res.value();

    if (proposed.post_state.accounts().size() != expected_post.accounts().size()) {
        return DomainError{DomainErrorKind::OrderValidationFailed,
                           "AccountStateMismatch: post-state account set differs from the reconstruction"};
    }

    for (const auto& [account_id, expected_acc] : expected_post.accounts()) {
        const AccountState* actual_acc = proposed.post_state.get_account(account_id);
        if (actual_acc == nullptr) {
            return DomainError{DomainErrorKind::OrderValidationFailed,
                               "AccountStateMismatch: account " +
                                   std::to_string(account_id.as_raw()) +
                                   " missing from proposed post-state"};
        }

        if (!(actual_acc->cash() == expected_acc.cash())) {
            return DomainError{DomainErrorKind::OrderValidationFailed,
                               "AccountStateMismatch: account " +
                                   std::to_string(account_id.as_raw()) +
                                   " cash does not match pre-state updated by its fills"};
        }

        if (!(actual_acc->inventory_of(inst) == expected_acc.inventory_of(inst))) {
            return DomainError{DomainErrorKind::OrderValidationFailed,
                               "AccountStateMismatch: account " +
                                   std::to_string(account_id.as_raw()) +
                                   " inventory does not match pre-state updated by its fills"};
        }
    }

    return ok;
}

} // namespace faircross
