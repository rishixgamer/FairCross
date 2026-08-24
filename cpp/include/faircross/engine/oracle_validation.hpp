#pragma once

#include <optional>

#include "faircross/domain/oracle.hpp"

namespace faircross {

/// Validates a reference-price snapshot against the policy governing it.
///
/// Includes an instrument-match check: without it a snapshot published for a
/// different instrument would be accepted as this batch's reference price.
inline Result<Ok> validate_reference_price_snapshot(
    const ReferencePricePolicy& policy,
    const ReferencePriceSnapshot& snapshot,
    InstrumentId expected_instrument,
    uint64_t batch_cutoff_nanos,
    std::optional<Price> clearing_price
) {
    if (!(snapshot.instrument_id == expected_instrument)) {
        return DomainError{DomainErrorKind::InstrumentMismatch,
                           "OracleValidationError: snapshot instrument does not match batch"};
    }

    auto fresh_res = policy.validate_freshness(snapshot, batch_cutoff_nanos);
    if (fresh_res.is_err()) return fresh_res;

    if (clearing_price.has_value()) {
        auto collar_res = policy.validate_collar(clearing_price.value(), snapshot);
        if (collar_res.is_err()) return collar_res;
    }

    return ok;
}

/// Convenience wrapper retaining the original argument order used by the
/// adversarial harness. Prefer `validate_reference_price_snapshot`.
inline Result<Ok> validate_oracle_reference(
    const ReferencePriceSnapshot& snapshot,
    const ReferencePricePolicy& policy,
    uint64_t batch_cutoff_nanos,
    std::optional<Price> clearing_price
) {
    return validate_reference_price_snapshot(
        policy, snapshot, snapshot.instrument_id, batch_cutoff_nanos, clearing_price);
}

} // namespace faircross
