#include "faircross/proof/constraints.hpp"
#include "faircross/engine/auction.hpp"
#include <algorithm>
#include <string>

namespace faircross {

Result<ProofComplexityMetrics> synthesize_clearing_optimality_constraints(
    ConstraintSystem& cs,
    const Batch& batch,
    std::optional<Price> clearing_price,
    Qty cleared_volume,
    std::vector<uint64_t>& scalars
) {
    // 1. Check the claimed outcome against the plaintext auction engine.
    auto honest_res = determine_clearing_outcome(batch);
    if (honest_res.is_err()) {
        return DomainError{DomainErrorKind::OrderValidationFailed,
                           "InvalidWitness: engine failed to determine clearing outcome"};
    }
    const ClearingOutcome& honest = honest_res.value();

    if (clearing_price != honest.clearing_price) {
        return DomainError{DomainErrorKind::OrderValidationFailed,
                           "clearing_price_optimality_and_tie_break"};
    }
    if (!(cleared_volume == honest.executable_volume)) {
        return DomainError{DomainErrorKind::OrderValidationFailed, "cleared_volume_maximality"};
    }

    // 2. Public inputs for the published clearing outcome.
    const Variable cp_pub_var = cs.alloc_public();
    const Variable vol_pub_var = cs.alloc_public();

    const uint64_t cp_raw = clearing_price.has_value() ? clearing_price->as_raw() : 0;
    const uint64_t vol_raw = cleared_volume.as_raw();

    // 3. One optimality slack per candidate price: no candidate clears more
    //    volume than the published V*.
    const std::vector<Price> prices = candidate_prices_from_orders(batch.orders());

    for (const Price& price : prices) {
        auto d_res = demand_from_orders(batch.orders(), price);
        if (d_res.is_err()) {
            return DomainError{DomainErrorKind::OrderValidationFailed,
                               "InvalidWitness: arithmetic overflow computing demand"};
        }
        auto s_res = supply_from_orders(batch.orders(), price);
        if (s_res.is_err()) {
            return DomainError{DomainErrorKind::OrderValidationFailed,
                               "InvalidWitness: arithmetic overflow computing supply"};
        }

        const uint64_t d = d_res.value().as_raw();
        const uint64_t s = s_res.value().as_raw();
        const uint64_t v = std::min(d, s);

        const Variable d_var = cs.alloc_witness();
        const Variable s_var = cs.alloc_witness();
        const Variable v_var = cs.alloc_witness();
        const Variable slack_var = cs.alloc_witness();

        scalars.push_back(d);
        scalars.push_back(s);
        scalars.push_back(v);
        scalars.push_back(vol_raw > v ? vol_raw - v : 0);

        const std::string price_tag = std::to_string(price.as_raw());

        LinearCombination lc_slack = LinearCombination::from_var(vol_pub_var);
        lc_slack.add_term(v_var, -1);
        cs.enforce_equal(lc_slack,
                         LinearCombination::from_var(slack_var),
                         "optimality_slack_at_price_" + price_tag);

        // Binds d and s to their claimed product. The constant is a 64-bit-by-64-bit
        // product, which is why coefficients must be 128-bit.
        cs.enforce(LinearCombination::from_var(d_var),
                   LinearCombination::from_var(s_var),
                   LinearCombination::from_constant(
                       wrapping_mul(static_cast<Scalar>(d), static_cast<Scalar>(s))),
                   "supply_demand_bound_" + price_tag);
    }

    // Bind the public clearing outcome to witness copies.
    const Variable cp_wit_var = cs.alloc_witness();
    const Variable vol_wit_var = cs.alloc_witness();
    scalars.push_back(cp_raw);
    scalars.push_back(vol_raw);

    cs.enforce_equal(LinearCombination::from_var(cp_wit_var),
                     LinearCombination::from_var(cp_pub_var),
                     "clearing_price_equality");
    cs.enforce_equal(LinearCombination::from_var(vol_wit_var),
                     LinearCombination::from_var(vol_pub_var),
                     "cleared_volume_equality");

    return ProofComplexityMetrics{
        cs.num_constraints(), cs.num_public(), cs.num_witness(), prices.size()};
}

} // namespace faircross
