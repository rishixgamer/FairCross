#include "faircross/proof/constraints.hpp"
#include <string>

namespace faircross {

Result<Ok> synthesize_complete_input_accounting_constraints(
    ConstraintSystem& cs,
    const Batch& batch,
    const std::vector<SaltedOrderPreimage>& preimages,
    const MerkleTree& merkle_tree,
    const CompleteInputAccounting& accounting,
    const BatchAllocation& allocation,
    std::vector<uint64_t>& scalars
) {
    // 1. Plaintext invariant auditor. The circuit proves an invariant the
    //    engine already checks; it does not redefine one.
    auto invariant = verify_complete_input_accounting(batch, merkle_tree, accounting, allocation);
    if (invariant.is_err()) {
        return DomainError{DomainErrorKind::OrderValidationFailed,
                           "complete_input_accounting_invariant: " + invariant.error_message()};
    }

    if (batch.len() != accounting.records.size()) {
        return DomainError{DomainErrorKind::OrderValidationFailed,
                           "accounting_cardinality: expected batch_size == " +
                               std::to_string(batch.len()) + ", got " +
                               std::to_string(accounting.records.size())};
    }

    const Commitment root_pub = merkle_tree.root();
    const Variable batch_len_var = cs.alloc_public();
    const Variable root_pub_var = cs.alloc_public();

    LinearCombination count_lc = LinearCombination::zero();

    for (size_t idx = 0; idx < accounting.records.size(); ++idx) {
        const OrderAccountingRecord& record = accounting.records[idx];
        const Commitment leaf = commit_order(preimages[idx]);

        // Sequential leaf index.
        const Variable seq_var = cs.alloc_witness();
        scalars.push_back(static_cast<uint64_t>(record.leaf_index));
        cs.enforce_equal(LinearCombination::from_var(seq_var),
                         LinearCombination::from_constant(static_cast<Scalar>(idx)),
                         "record_" + std::to_string(idx) + "_sequential_index");

        // Merkle inclusion against the shared public root variable.
        auto proof = merkle_tree.generate_proof(idx);
        if (!proof.has_value()) {
            return DomainError{DomainErrorKind::OrderValidationFailed,
                               "MerkleProofInvalid: leaf_index " + std::to_string(idx)};
        }
        auto incl = synthesize_merkle_inclusion_constraint_with_root_var(
            cs, root_pub_var, root_pub, leaf, proof.value(), scalars);
        if (incl.is_err()) {
            return incl;
        }

        // Disposition must be the one implied by the allocation.
        const OrderAllocation* alloc = nullptr;
        for (const auto& a : allocation.allocations) {
            if (a.order_id == record.order_id) {
                alloc = &a;
                break;
            }
        }
        if (alloc == nullptr) {
            return DomainError{DomainErrorKind::OrderValidationFailed,
                               "InvalidWitness: order not found in allocation"};
        }

        OrderDisposition expected = OrderDisposition::unfilled();
        if (alloc->allocated_qty.as_raw() == 0) {
            expected = OrderDisposition::unfilled();
        } else if (alloc->allocated_qty == alloc->original_qty) {
            expected = OrderDisposition::fully_executed(alloc->allocated_qty);
        } else {
            const uint64_t original = alloc->original_qty.as_raw();
            const uint64_t allocated = alloc->allocated_qty.as_raw();
            const uint64_t unfilled = original > allocated ? original - allocated : 0;
            expected = OrderDisposition::partially_executed(alloc->allocated_qty,
                                                            Qty::from_raw(unfilled));
        }

        if (!(record.disposition == expected)) {
            return DomainError{DomainErrorKind::OrderValidationFailed,
                               "record_" + std::to_string(idx) + "_disposition_match"};
        }

        // Unit term contributing to the cardinality total.
        count_lc.add_term(Variable::one(), 1);
    }

    cs.enforce_equal(count_lc,
                     LinearCombination::from_var(batch_len_var),
                     "total_accounting_cardinality_equality");

    return ok;
}

} // namespace faircross
