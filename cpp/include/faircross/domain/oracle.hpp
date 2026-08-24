#pragma once

#include <cstdint>
#include "faircross/domain/primitives.hpp"

namespace faircross {

/// An authenticated reference price snapshot from an external oracle or market feed.
struct ReferencePriceSnapshot {
    uint32_t oracle_id;
    InstrumentId instrument_id;
    Price reference_price;
    uint64_t timestamp_nanos;
    uint64_t sequence;

    ReferencePriceSnapshot()
        : oracle_id(0), instrument_id(0), reference_price(Price(1)), timestamp_nanos(0), sequence(0) {}

    ReferencePriceSnapshot(
        uint32_t o_id,
        InstrumentId inst,
        Price ref_price,
        uint64_t ts,
        uint64_t seq
    ) : oracle_id(o_id), instrument_id(inst), reference_price(ref_price), timestamp_nanos(ts), sequence(seq) {}

    auto operator<=>(const ReferencePriceSnapshot&) const = default;
};

/// Deterministic freshness and price collar policy for reference data.
class ReferencePricePolicy {
public:
    uint64_t max_staleness_nanos;
    uint64_t max_deviation_ticks;

    ReferencePricePolicy(uint64_t max_staleness, uint64_t max_deviation)
        : max_staleness_nanos(max_staleness), max_deviation_ticks(max_deviation) {}

    Result<Ok> validate_freshness(const ReferencePriceSnapshot& snapshot, uint64_t batch_cutoff_nanos) const {
        if (snapshot.timestamp_nanos > batch_cutoff_nanos) {
            return DomainError{DomainErrorKind::OrderValidationFailed, "FutureOracleSnapshot"};
        }
        uint64_t age = batch_cutoff_nanos - snapshot.timestamp_nanos;
        if (age > max_staleness_nanos) {
            return DomainError{DomainErrorKind::OrderValidationFailed, "StaleOracleSnapshot"};
        }
        return ok;
    }

    Result<Ok> validate_collar(Price clearing_price, const ReferencePriceSnapshot& snapshot) const {
        uint64_t p_clear = clearing_price.as_raw();
        uint64_t p_ref = snapshot.reference_price.as_raw();
        uint64_t diff = (p_clear >= p_ref) ? (p_clear - p_ref) : (p_ref - p_clear);
        if (diff > max_deviation_ticks) {
            return DomainError{DomainErrorKind::OrderValidationFailed, "PriceCollarBreach"};
        }
        return ok;
    }
};

} // namespace faircross
