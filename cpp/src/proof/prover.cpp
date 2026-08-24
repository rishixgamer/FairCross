#include "faircross/proof/prover.hpp"
#include "faircross/proof/constraints.hpp"
#include "faircross/engine/checker.hpp"
#include "faircross/commitments/batch_commitment.hpp"
#include "faircross/commitments/ledger_commitment.hpp"
#include "faircross/commitments/oracle_commitment.hpp"
#include <limits>
#include <sstream>

namespace faircross {

Result<BatchProof> SingleBatchProver::prove(
    const BatchProofPublicInputs& public_inputs,
    const BatchProofWitness& witness
) {
    // 1. The witness must describe exactly the canonical batch order. This
    //    check is deliberately before any transition/proof work: otherwise a
    //    prover could commit to a different set or ordering of leaves than
    //    the batch whose transition is being proved.
    const size_t batch_len = witness.batch.len();
    if (witness.preimages.size() != batch_len) {
        return DomainError{DomainErrorKind::OrderValidationFailed,
                           "InputBindingFailed: preimage count does not match batch length"};
    }
    if (batch_len > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
        return DomainError{DomainErrorKind::OrderValidationFailed,
                           "InputBindingFailed: batch length exceeds uint32 order count"};
    }

    // 2. Build the Merkle tree over the committed order preimages and bind
    //    each preimage to the corresponding canonical batch leaf.
    std::vector<Commitment> leaves;
    leaves.reserve(witness.preimages.size());
    for (size_t idx = 0; idx < batch_len; ++idx) {
        if (witness.preimages[idx].order != witness.batch.orders()[idx]) {
            return DomainError{DomainErrorKind::OrderValidationFailed,
                               "InputBindingFailed: preimage order does not match batch leaf"};
        }
        leaves.push_back(commit_order(witness.preimages[idx]));
    }
    MerkleTree merkle_tree(leaves);

    // Reconstruct the header from witness data. A caller-supplied header hash
    // is not trusted as a free public variable.
    const BatchHeader reconstructed_header(
        1,
        witness.batch.batch_id(),
        witness.batch.instrument(),
        witness.batch_cutoff_nanos,
        static_cast<uint32_t>(batch_len),
        merkle_tree.root());
    if (compute_batch_commitment(reconstructed_header) != public_inputs.batch_header_hash) {
        return DomainError{DomainErrorKind::OrderValidationFailed,
                           "PublicInputMismatch: batch_header_hash"};
    }

    // Accounting records are allowed to arrive in any vector order, but each
    // leaf index has one canonical record. Normalize by leaf index before
    // passing the manifest to the constraint synthesizer, which consumes
    // records and preimages positionally.
    if (witness.accounting.records.size() != batch_len) {
        return DomainError{DomainErrorKind::OrderValidationFailed,
                           "InputBindingFailed: accounting count does not match batch length"};
    }
    std::vector<OrderAccountingRecord> canonical_records(batch_len);
    std::vector<bool> present(batch_len, false);
    for (const auto& record : witness.accounting.records) {
        if (record.leaf_index >= batch_len || present[record.leaf_index]) {
            return DomainError{DomainErrorKind::OrderValidationFailed,
                               "InputBindingFailed: invalid accounting leaf index"};
        }
        canonical_records[record.leaf_index] = record;
        present[record.leaf_index] = true;
    }
    for (size_t idx = 0; idx < batch_len; ++idx) {
        if (!present[idx]) {
            return DomainError{DomainErrorKind::OrderValidationFailed,
                               "InputBindingFailed: omitted accounting leaf"};
        }
        const auto& record = canonical_records[idx];
        if (record.order_id != witness.batch.orders()[idx].id) {
            return DomainError{DomainErrorKind::OrderValidationFailed,
                               "InputBindingFailed: accounting order does not match batch leaf"};
        }
        if (record.commitment != leaves[idx]) {
            return DomainError{DomainErrorKind::OrderValidationFailed,
                               "InputBindingFailed: accounting commitment does not match preimage"};
        }
    }
    CompleteInputAccounting canonical_accounting = witness.accounting;
    canonical_accounting.records = std::move(canonical_records);

    // 3. Plaintext invariant verification via the engine's checker. Every
    //    subrelation below proves an invariant that already holds here; the
    //    circuit never introduces market semantics of its own.
    std::optional<Price> clearing_price_opt = std::nullopt;
    if (public_inputs.clearing_price > 0) {
        clearing_price_opt = Price(public_inputs.clearing_price);
    }
    const Qty cleared_vol = Qty::from_raw(public_inputs.cleared_volume);

    BatchExecutionResult exec_result{
        ClearingOutcome{clearing_price_opt, cleared_vol},
        witness.allocation,
        witness.fills,
        witness.post_state
    };

    auto verify_res = verify_transition(witness.pre_state, witness.batch, exec_result);
    if (verify_res.is_err()) {
        return DomainError{DomainErrorKind::OrderValidationFailed,
                           "StateTransitionInvariantFailed: " + verify_res.error_message()};
    }

    auto accounting_res = verify_complete_input_accounting(
        witness.batch, merkle_tree, canonical_accounting, witness.allocation);
    if (accounting_res.is_err()) {
        return DomainError{DomainErrorKind::OrderValidationFailed,
                           "CompleteInputAccountingFailed: " + accounting_res.error_message()};
    }

    // 3c. Bind the published state roots to the witness ledgers.
    //
    //     Ordered after the invariant checks deliberately: a malicious
    //     transition should be reported as the invariant it violates, which is
    //     what the adversarial evidence table records, not as a root mismatch.
    //     Without this binding the roots are free variables and a prover could
    //     publish any digest alongside a valid transition.
    const InstrumentId instrument = witness.batch.instrument();
    if (!(public_inputs.pre_state_root == compute_ledger_root(witness.pre_state, instrument))) {
        return DomainError{DomainErrorKind::OrderValidationFailed,
                           "PublicInputMismatch: pre_state_root"};
    }
    if (!(public_inputs.post_state_root == compute_ledger_root(witness.post_state, instrument))) {
        return DomainError{DomainErrorKind::OrderValidationFailed,
                           "PublicInputMismatch: post_state_root"};
    }

    // 3d. Bind the oracle snapshot. Absence is committed explicitly, so a batch
    //     cleared without reference data is a different published statement from
    //     one whose snapshot was simply omitted.
    if (!(public_inputs.oracle_snapshot_hash ==
          compute_oracle_snapshot_commitment(witness.oracle_snapshot, witness.oracle_policy))) {
        return DomainError{DomainErrorKind::OrderValidationFailed,
                           "PublicInputMismatch: oracle_snapshot_hash"};
    }

    // 4. Compose the R1CS constraint subrelations.
    //
    // Order matters: public variables are allocated 0..4 across steps 3b, 3c,
    // and 3d, and the witness vector is the concatenation of each subrelation's
    // scalars in synthesis order. Reordering changes the certificate.
    ConstraintSystem cs;

    // 3a. Order validity (witness variables only).
    std::vector<uint64_t> order_wit;
    synthesize_order_validity_constraints(
        cs, witness.batch.instrument().as_raw(), witness.batch.orders(), order_wit);

    // 3b. Complete-input accounting and Merkle inclusion. Allocates public 0, 1.
    std::vector<uint64_t> acct_wit;
    auto acct_res = synthesize_complete_input_accounting_constraints(
        cs, witness.batch, witness.preimages, merkle_tree, canonical_accounting,
        witness.allocation, acct_wit);
    if (acct_res.is_err()) {
        return DomainError{DomainErrorKind::OrderValidationFailed, acct_res.error_message()};
    }

    // 4c. Clearing volume optimality. Allocates public 2, 3.
    std::vector<uint64_t> opt_wit;
    auto opt_res = synthesize_clearing_optimality_constraints(
        cs, witness.batch, clearing_price_opt, cleared_vol, opt_wit);
    if (opt_res.is_err()) {
        return DomainError{DomainErrorKind::OrderValidationFailed, opt_res.error_message()};
    }

    // 4d. Deterministic allocation, only when the book crossed. Allocates public 4.
    std::vector<uint64_t> alloc_wit;
    if (clearing_price_opt.has_value()) {
        auto alloc_res = synthesize_allocation_constraints(
            cs, witness.batch, clearing_price_opt.value(), cleared_vol, witness.allocation,
            alloc_wit);
        if (alloc_res.is_err()) {
            return DomainError{DomainErrorKind::OrderValidationFailed, alloc_res.error_message()};
        }
    }

    // 4e. Fill bounds and limit price compliance.
    std::vector<uint64_t> fills_wit;
    if (clearing_price_opt.has_value()) {
        auto fills_res = synthesize_fill_bounds_constraints(
            cs, clearing_price_opt.value(), witness.batch.orders(), witness.fills, fills_wit);
        if (fills_res.is_err()) {
            return DomainError{DomainErrorKind::OrderValidationFailed, fills_res.error_message()};
        }
    }

    // 4f. Conservation of cash and inventory.
    std::vector<uint64_t> conserv_wit;
    auto conserv_res = synthesize_conservation_constraints(
        cs, witness.pre_state, witness.post_state, witness.fills, witness.batch.instrument(),
        conserv_wit);
    if (conserv_res.is_err()) {
        return DomainError{DomainErrorKind::OrderValidationFailed, conserv_res.error_message()};
    }

    // 4g. Oracle freshness and price collar. Appended last so batches without an
    //     oracle preserve the preceding subrelations' variable indices.
    std::vector<uint64_t> oracle_wit;
    if (witness.oracle_snapshot.has_value() && witness.oracle_policy.has_value() &&
        clearing_price_opt.has_value()) {
        auto oracle_res = synthesize_oracle_freshness_constraints(
            cs, witness.batch.instrument(), witness.batch_cutoff_nanos,
            clearing_price_opt.value(), witness.oracle_policy.value(), witness.oracle_snapshot,
            oracle_wit);
        if (oracle_res.is_err()) {
            return DomainError{DomainErrorKind::OrderValidationFailed, oracle_res.error_message()};
        }
    }

    // 5. Concatenate the witness vector in synthesis order and check satisfaction.
    std::vector<uint64_t> full_witness;
    full_witness.reserve(order_wit.size() + acct_wit.size() + opt_wit.size() +
                         alloc_wit.size() + fills_wit.size() + conserv_wit.size() +
                         oracle_wit.size());
    for (const auto* part : {&order_wit, &acct_wit, &opt_wit, &alloc_wit, &fills_wit, &conserv_wit, &oracle_wit}) {
        full_witness.insert(full_witness.end(), part->begin(), part->end());
    }

    // The Merkle root enters the constraint system as its low 64 bits, bound to
    // public variable 1 by every per-leaf inclusion constraint.
    uint64_t root_scalar = 0;
    const Commitment root = merkle_tree.root();
    for (size_t i = 0; i < 8; ++i) {
        root_scalar |= static_cast<uint64_t>(root.bytes[i]) << (i * 8);
    }

    const std::vector<uint64_t> public_scalars = {
        static_cast<uint64_t>(witness.batch.len()),
        root_scalar,
        public_inputs.clearing_price,
        public_inputs.cleared_volume,
        public_inputs.cleared_volume
    };

    auto sat_res = cs.is_satisfied(public_scalars, full_witness);
    if (sat_res.is_err()) {
        return DomainError{DomainErrorKind::OrderValidationFailed,
                           "R1CSUnsatisfied: " + sat_res.error_message()};
    }

    // 6. Build the certificate artifact.
    std::ostringstream oss;
    oss << "FC-R1CS-V1:constraints=" << cs.num_constraints()
        << ":public_inputs_len=" << public_scalars.size()
        << ":status=SAT";
    const std::string proof_str = oss.str();
    std::vector<uint8_t> proof_bytes(proof_str.begin(), proof_str.end());

    return BatchProof{
        1,
        public_inputs,
        std::move(proof_bytes)
    };
}

} // namespace faircross
