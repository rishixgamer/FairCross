#include "faircross/proof/constraints.hpp"
#include <string>

namespace faircross {

Result<Ok> synthesize_conservation_constraints(
    ConstraintSystem& cs,
    const Ledger& pre_state,
    const Ledger& post_state,
    const std::vector<Fill>& fills,
    InstrumentId instrument,
    std::vector<uint64_t>& scalars
) {
    LinearCombination total_pre_cash_lc = LinearCombination::zero();
    LinearCombination total_post_cash_lc = LinearCombination::zero();
    LinearCombination total_pre_inv_lc = LinearCombination::zero();
    LinearCombination total_post_inv_lc = LinearCombination::zero();

    // Iteration follows the ledger's ordered account map. Variable indices
    // depend on this order, so it is part of the constraint system's contract.
    for (const auto& [acc_id, acc] : pre_state.accounts()) {
        const auto pre_cash = static_cast<uint64_t>(acc.cash().as_raw());
        const uint64_t pre_inv = acc.inventory_of(instrument).as_raw();

        const AccountState* post_acc = post_state.get_account(acc_id);
        const uint64_t post_cash =
            post_acc != nullptr ? static_cast<uint64_t>(post_acc->cash().as_raw()) : 0;
        const uint64_t post_inv =
            post_acc != nullptr ? post_acc->inventory_of(instrument).as_raw() : 0;

        const Variable pre_cash_var = cs.alloc_witness();
        const Variable post_cash_var = cs.alloc_witness();
        const Variable pre_inv_var = cs.alloc_witness();
        const Variable post_inv_var = cs.alloc_witness();

        scalars.push_back(pre_cash);
        scalars.push_back(post_cash);
        scalars.push_back(pre_inv);
        scalars.push_back(post_inv);

        total_pre_cash_lc.add_term(pre_cash_var, 1);
        total_post_cash_lc.add_term(post_cash_var, 1);
        total_pre_inv_lc.add_term(pre_inv_var, 1);
        total_post_inv_lc.add_term(post_inv_var, 1);

        // Per-account transition implied by that account's fills.
        LinearCombination expected_post_cash_lc = LinearCombination::from_var(pre_cash_var);
        LinearCombination expected_post_inv_lc = LinearCombination::from_var(pre_inv_var);

        for (const Fill& fill : fills) {
            if (!(fill.account_id == acc_id)) {
                continue;
            }
            const Variable consideration_var = cs.alloc_witness();
            const Variable fill_qty_var = cs.alloc_witness();

            scalars.push_back(static_cast<uint64_t>(fill.consideration.as_raw()));
            scalars.push_back(fill.fill_qty.as_raw());

            if (fill.side == Side::Buy) {
                expected_post_cash_lc.add_term(consideration_var, -1);
                expected_post_inv_lc.add_term(fill_qty_var, 1);
            } else {
                expected_post_cash_lc.add_term(consideration_var, 1);
                expected_post_inv_lc.add_term(fill_qty_var, -1);
            }
        }

        const std::string acc_tag = std::to_string(acc_id.as_raw());

        cs.enforce_equal(LinearCombination::from_var(post_cash_var),
                         expected_post_cash_lc,
                         "account_" + acc_tag + "_cash_transition");

        cs.enforce_equal(LinearCombination::from_var(post_inv_var),
                         expected_post_inv_lc,
                         "account_" + acc_tag + "_inv_transition");
    }

    // No cash or asset may be created or destroyed across the batch.
    cs.enforce_equal(total_pre_cash_lc, total_post_cash_lc, "global_cash_conservation");
    cs.enforce_equal(total_pre_inv_lc, total_post_inv_lc, "global_asset_conservation");

    return ok;
}

} // namespace faircross
