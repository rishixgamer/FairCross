#pragma once

// R1CS constraint subrelations for FairCross single-batch proofs.
//
// Each subrelation encodes an invariant that the plaintext engine already
// proves; none of them invent market semantics. The synthesis order, variable
// allocation order, and constraint count are all observable in the proof
// certificate and are pinned by `test_golden_r1cs_constraint_counts`, so a
// subrelation must not be reordered or have constraints added without
// deliberately re-freezing the golden vectors.

#include <vector>
#include <optional>
#include <cstdint>

#include "faircross/domain/primitives.hpp"
#include "faircross/domain/order.hpp"
#include "faircross/domain/fill.hpp"
#include "faircross/domain/ledger.hpp"
#include "faircross/commitments/merkle.hpp"
#include "faircross/engine/batch.hpp"
#include "faircross/engine/allocation.hpp"
#include "faircross/engine/accounting.hpp"
#include "faircross/domain/oracle.hpp"
#include "faircross/proof/r1cs.hpp"

namespace faircross {

/// Circuit variables allocated for a single order.
struct AllocatedOrderVar {
    Variable id;
    Variable account;
    Variable instrument;
    Variable side;
    Variable price;
    Variable qty;
    Variable sequence;
};

/// Complexity counters reported alongside clearing-optimality synthesis.
struct ProofComplexityMetrics {
    size_t num_constraints;
    size_t num_public_variables;
    size_t num_witness_variables;
    size_t num_candidate_prices;
};

/// Order validity: boolean side, instrument match, sequence monotonicity, and
/// strict positivity of price and quantity.
///
/// Allocates only witness variables. Appends its witness scalars to `scalars`.
std::vector<AllocatedOrderVar> synthesize_order_validity_constraints(
    ConstraintSystem& cs,
    uint64_t batch_instrument,
    const std::vector<Order>& orders,
    std::vector<uint64_t>& scalars
);

/// Merkle inclusion of one leaf against an already-allocated root variable.
///
/// Folds the authentication path in plaintext, then constrains the low 64 bits
/// of the recomputed root to equal the public root variable. This is what binds
/// the committed order set to the published root.
Result<Ok> synthesize_merkle_inclusion_constraint_with_root_var(
    ConstraintSystem& cs,
    Variable root_pub_var,
    const Commitment& expected_root,
    const Commitment& leaf,
    const MerkleProof& proof,
    std::vector<uint64_t>& scalars
);

/// Complete-input accounting: every committed order is present exactly once,
/// included in the Merkle root, and carries the disposition implied by its
/// allocation.
///
/// Allocates public variables 0 (batch length) and 1 (Merkle root projection).
Result<Ok> synthesize_complete_input_accounting_constraints(
    ConstraintSystem& cs,
    const Batch& batch,
    const std::vector<SaltedOrderPreimage>& preimages,
    const MerkleTree& merkle_tree,
    const CompleteInputAccounting& accounting,
    const BatchAllocation& allocation,
    std::vector<uint64_t>& scalars
);

/// Clearing optimality: the published price and volume maximize executable
/// volume across every candidate price, and satisfy the tie-break rule.
///
/// Allocates public variables 2 (clearing price) and 3 (cleared volume).
Result<ProofComplexityMetrics> synthesize_clearing_optimality_constraints(
    ConstraintSystem& cs,
    const Batch& batch,
    std::optional<Price> clearing_price,
    Qty cleared_volume,
    std::vector<uint64_t>& scalars
);

/// Deterministic allocation: allocations match the canonical allocator, respect
/// moneyness rules, and sum to the cleared volume on both sides.
///
/// Allocates public variable 4 (cleared volume, again).
Result<Ok> synthesize_allocation_constraints(
    ConstraintSystem& cs,
    const Batch& batch,
    Price clearing_price,
    Qty cleared_volume,
    const BatchAllocation& allocation,
    std::vector<uint64_t>& scalars
);

/// Fill bounds: no overfill, correct consideration, and limit-price compliance.
Result<Ok> synthesize_fill_bounds_constraints(
    ConstraintSystem& cs,
    Price clearing_price,
    const std::vector<Order>& orders,
    const std::vector<Fill>& fills,
    std::vector<uint64_t>& scalars
);

/// Conservation: per-account cash and inventory transitions follow from the
/// fills, and neither total is created or destroyed.
Result<Ok> synthesize_conservation_constraints(
    ConstraintSystem& cs,
    const Ledger& pre_state,
    const Ledger& post_state,
    const std::vector<Fill>& fills,
    InstrumentId instrument,
    std::vector<uint64_t>& scalars
);

/// Oracle freshness and price collar, when the batch cleared against reference
/// data. Emits nothing when no snapshot is supplied.
Result<Ok> synthesize_oracle_freshness_constraints(
    ConstraintSystem& cs,
    InstrumentId instrument,
    uint64_t batch_timestamp_nanos,
    Price clearing_price,
    const ReferencePricePolicy& policy,
    const std::optional<ReferencePriceSnapshot>& snapshot,
    std::vector<uint64_t>& scalars
);

} // namespace faircross
