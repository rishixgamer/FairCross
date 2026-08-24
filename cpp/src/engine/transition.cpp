#include "faircross/engine/transition.hpp"
#include <set>

namespace faircross {

Result<Ledger> apply_batch_transition(
    const Ledger& pre_state,
    const std::vector<Fill>& fills
) {
    Ledger post_state = pre_state;

    for (const auto& fill : fills) {
        auto& acc = post_state.get_or_create_account(fill.account_id);
        if (fill.side == Side::Buy) {
            // Buyer spends cash and receives inventory.
            auto deb_res = acc.debit_cash(fill.consideration);
            if (deb_res.is_err()) return deb_res;
            auto cred_res = acc.credit_inventory(fill.instrument_id, fill.fill_qty);
            if (cred_res.is_err()) return cred_res;
        } else {
            // Seller delivers inventory and receives cash.
            auto deb_res = acc.debit_inventory(fill.instrument_id, fill.fill_qty);
            if (deb_res.is_err()) return deb_res;
            auto cred_res = acc.credit_cash(fill.consideration);
            if (cred_res.is_err()) return cred_res;
        }
    }

    auto pre_cash = pre_state.total_cash();
    auto post_cash = post_state.total_cash();
    if (pre_cash.is_err() || post_cash.is_err() || pre_cash.value() != post_cash.value()) {
        return DomainError{DomainErrorKind::OrderValidationFailed,
                           "InvariantViolation: cash_conservation"};
    }

    std::set<InstrumentId> touched;
    for (const auto& f : fills) touched.insert(f.instrument_id);
    for (const auto& inst : touched) {
        auto pre_inv = pre_state.total_inventory_of(inst);
        auto post_inv = post_state.total_inventory_of(inst);
        if (pre_inv.is_err() || post_inv.is_err() || pre_inv.value() != post_inv.value()) {
            return DomainError{DomainErrorKind::OrderValidationFailed,
                               "InvariantViolation: asset_conservation"};
        }
    }

    return post_state;
}

Result<BatchExecutionResult> execute_batch(
    const Ledger& pre_state,
    const Batch& batch
) {
    auto outcome_res = determine_clearing_outcome(batch);
    if (outcome_res.is_err()) return outcome_res;
    ClearingOutcome outcome = outcome_res.value();

    auto alloc_res = allocate_batch(batch, outcome.clearing_price, outcome.executable_volume);
    if (alloc_res.is_err()) return alloc_res;
    BatchAllocation allocation = alloc_res.value();

    auto fills_res = generate_canonical_fills(batch, allocation);
    if (fills_res.is_err()) return fills_res;
    std::vector<Fill> fills = fills_res.value();

    auto post_res = apply_batch_transition(pre_state, fills);
    if (post_res.is_err()) return post_res;
    Ledger post_state = post_res.value();

    return BatchExecutionResult{
        outcome,
        std::move(allocation),
        std::move(fills),
        std::move(post_state)
    };
}

} // namespace faircross
