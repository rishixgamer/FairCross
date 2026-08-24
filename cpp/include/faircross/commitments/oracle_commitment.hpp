#pragma once

// Oracle reference-price snapshot commitment.
//
// `oracle_snapshot_hash` is a published input of the batch proof statement and
// binds the reference prices a batch cleared against.
//
// The commitment covers the snapshot *and the policy it was validated under*,
// since a reference price is only meaningful relative to the staleness bound and
// price collar that admitted it.

#include <optional>
#include <string_view>

#include "faircross/domain/encoding.hpp"
#include "faircross/domain/oracle.hpp"
#include "faircross/commitments/sha256.hpp"

namespace faircross {

/// Domain-separated marker committing to the deliberate absence of a snapshot.
///
/// A batch cleared without reference data must be distinguishable from one whose
/// snapshot field was left unset, which a zero digest cannot express.
inline constexpr std::string_view ABSENT_ORACLE_MARKER = "FCOS_ABSENT_V1";

inline Commitment compute_oracle_snapshot_commitment(
    const std::optional<ReferencePriceSnapshot>& snapshot,
    const std::optional<ReferencePricePolicy>& policy
) {
    if (snapshot.has_value() && policy.has_value()) {
        const OracleSnapshotPreimage preimage(snapshot.value(), policy.value());
        return Sha256Scheme::commit_raw_bytes(canonical_encode_oracle_snapshot(preimage));
    }
    const auto* bytes = reinterpret_cast<const uint8_t*>(ABSENT_ORACLE_MARKER.data());
    return Sha256Scheme::commit_raw_bytes(bytes, ABSENT_ORACLE_MARKER.size());
}

} // namespace faircross
