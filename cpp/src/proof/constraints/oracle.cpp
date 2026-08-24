#include "faircross/proof/constraints.hpp"
#include "faircross/engine/oracle_validation.hpp"

namespace faircross {

Result<Ok> synthesize_oracle_freshness_constraints(
    ConstraintSystem& cs,
    InstrumentId instrument,
    uint64_t batch_timestamp_nanos,
    Price clearing_price,
    const ReferencePricePolicy& policy,
    const std::optional<ReferencePriceSnapshot>& snapshot,
    std::vector<uint64_t>& scalars
) {
    if (!snapshot.has_value()) {
        return ok;
    }
    const ReferencePriceSnapshot& snap = snapshot.value();

    // 1. Plaintext policy validation. The circuit proves an invariant the engine
    //    already checks; it does not define freshness itself.
    auto valid = validate_reference_price_snapshot(
        policy, snap, instrument, batch_timestamp_nanos, std::optional<Price>(clearing_price));
    if (valid.is_err()) {
        return DomainError{DomainErrorKind::OrderValidationFailed,
                           "oracle_policy_validation: " + valid.error_message()};
    }

    const Variable t_batch_var = cs.alloc_witness();
    const Variable t_oracle_var = cs.alloc_witness();
    [[maybe_unused]] const Variable cp_var = cs.alloc_witness();
    [[maybe_unused]] const Variable p_oracle_var = cs.alloc_witness();

    const uint64_t cp_val = clearing_price.as_raw();
    const uint64_t p_oracle_val = snap.reference_price.as_raw();

    scalars.push_back(batch_timestamp_nanos);
    scalars.push_back(snap.timestamp_nanos);
    scalars.push_back(cp_val);
    scalars.push_back(p_oracle_val);

    // 1. Non-future timestamp: t_batch - t_oracle = age, with age >= 0.
    const uint64_t age = batch_timestamp_nanos > snap.timestamp_nanos
                             ? batch_timestamp_nanos - snap.timestamp_nanos
                             : 0;
    const Variable age_var = cs.alloc_witness();
    scalars.push_back(age);

    LinearCombination lc_age = LinearCombination::from_var(t_batch_var);
    lc_age.add_term(t_oracle_var, -1);
    cs.enforce_equal(lc_age, LinearCombination::from_var(age_var), "oracle_timestamp_delta");

    // 2. Freshness: max_staleness - age = slack, with slack >= 0.
    const uint64_t max_staleness = policy.max_staleness_nanos;
    const uint64_t fresh_slack = max_staleness > age ? max_staleness - age : 0;
    const Variable fresh_slack_var = cs.alloc_witness();
    scalars.push_back(fresh_slack);

    LinearCombination lc_fresh = LinearCombination::from_constant(static_cast<Scalar>(max_staleness));
    lc_fresh.add_term(age_var, -1);
    cs.enforce_equal(lc_fresh, LinearCombination::from_var(fresh_slack_var),
                     "oracle_freshness_slack");

    // 3. Price collar: max_collar - |cp - p_oracle| = slack, with slack >= 0.
    const uint64_t diff =
        cp_val > p_oracle_val ? cp_val - p_oracle_val : p_oracle_val - cp_val;
    const Variable diff_var = cs.alloc_witness();
    scalars.push_back(diff);

    const uint64_t max_collar = policy.max_deviation_ticks;
    const uint64_t collar_slack = max_collar > diff ? max_collar - diff : 0;
    const Variable collar_slack_var = cs.alloc_witness();
    scalars.push_back(collar_slack);

    LinearCombination lc_collar = LinearCombination::from_constant(static_cast<Scalar>(max_collar));
    lc_collar.add_term(diff_var, -1);
    cs.enforce_equal(lc_collar, LinearCombination::from_var(collar_slack_var),
                     "oracle_collar_slack");

    return ok;
}

} // namespace faircross
