#pragma once

#include <vector>
#include <string>
#include "faircross/domain/ledger.hpp"
#include "faircross/domain/fill.hpp"
#include "faircross/commitments/commitment.hpp"
#include "faircross/engine/batch.hpp"
#include "faircross/engine/allocation.hpp"
#include "faircross/engine/accounting.hpp"
#include "faircross/domain/oracle.hpp"
#include <optional>

namespace faircross {

/// Public inputs for the single-batch transition constraint statement v0.
struct BatchProofPublicInputs {
    Commitment pre_state_root;
    Commitment post_state_root;
    Commitment batch_header_hash;
    Commitment oracle_snapshot_hash;
    uint64_t clearing_price;
    uint64_t cleared_volume;

    auto operator<=>(const BatchProofPublicInputs&) const = default;
};

/// Private witness supplying all preimages and execution data.
struct BatchProofWitness {
    Ledger pre_state;
    Ledger post_state;
    Batch batch;
    std::vector<SaltedOrderPreimage> preimages;
    BatchAllocation allocation;
    std::vector<Fill> fills;
    CompleteInputAccounting accounting;
    /// Reference-price snapshot the batch cleared against, if any.
    ///
    /// Absent means the batch cleared without reference data, which is a
    /// distinct published statement from "a snapshot exists but is unstated":
    /// the two commit to different `oracle_snapshot_hash` values.
    std::optional<ReferencePriceSnapshot> oracle_snapshot;
    /// Freshness and collar policy the snapshot was validated under.
    std::optional<ReferencePricePolicy> oracle_policy;
    /// Batch cutoff used for the freshness comparison.
    uint64_t batch_cutoff_nanos = 0;
};

/// Serialized certificate from the transparent in-tree R1CS harness.
///
/// This is not a zero-knowledge or cryptographically sound proof artifact.
struct BatchProof {
    uint8_t proof_version;
    BatchProofPublicInputs public_inputs;
    std::vector<uint8_t> proof_bytes;

    auto operator<=>(const BatchProof&) const = default;
};

} // namespace faircross
