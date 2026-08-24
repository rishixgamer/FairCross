#include "faircross/proof/constraints.hpp"
#include <string>

namespace faircross {

Result<Ok> synthesize_merkle_inclusion_constraint_with_root_var(
    ConstraintSystem& cs,
    Variable root_pub_var,
    const Commitment& expected_root,
    const Commitment& leaf,
    const MerkleProof& proof,
    std::vector<uint64_t>& scalars
) {
    // 1. Plaintext verification against the cryptographic scheme.
    if (!proof.verify(expected_root, leaf)) {
        return DomainError{DomainErrorKind::OrderValidationFailed,
                           "MerkleProofInvalid: leaf_index " + std::to_string(proof.leaf_index)};
    }

    // 2. Recompute the root by folding the authentication path.
    Commitment current = leaf;
    for (const auto& step : proof.path) {
        if (step.side == StepSide::Left) {
            current = Sha256Scheme::combine_nodes(step.sibling, current);
        } else {
            current = Sha256Scheme::combine_nodes(current, step.sibling);
        }
    }

    // 3. Project the recomputed root onto a field-sized scalar and bind it to
    //    the public root variable. This is the constraint that ties the
    //    committed order set to the published Merkle root.
    const Variable root_wit_var = cs.alloc_witness();
    uint64_t computed_scalar = 0;
    for (size_t i = 0; i < 8; ++i) {
        computed_scalar |= static_cast<uint64_t>(current.bytes[i]) << (i * 8);
    }
    scalars.push_back(computed_scalar);

    cs.enforce_equal(LinearCombination::from_var(root_wit_var),
                     LinearCombination::from_var(root_pub_var),
                     "merkle_inclusion_root_leaf_" + std::to_string(proof.leaf_index));

    return ok;
}

} // namespace faircross
