#include "faircross/proof/recursive/verifier.hpp"
#include "faircross/proof/verifier.hpp"
#include "faircross/commitments/sha256.hpp"
#include "faircross/domain/encoding.hpp"

namespace faircross {

Result<Ok> RecursiveSessionVerifier::verify_session(
    const SessionProofPublicInputs& expected_public_inputs,
    const SessionProof& proof
) {
    if (proof.session_version != 1) {
        return DomainError{DomainErrorKind::OrderValidationFailed,
                           "UnsupportedProofVersion: " + std::to_string(proof.session_version)};
    }

    if (!(proof.public_inputs == expected_public_inputs)) {
        return DomainError{DomainErrorKind::OrderValidationFailed,
                           "PublicInputMismatch: session_public_inputs"};
    }

    if (proof.step_proofs.size() != proof.num_batches || proof.step_proofs.empty()) {
        return DomainError{DomainErrorKind::OrderValidationFailed,
                           "InvalidProofArtifact: step proofs count mismatch"};
    }

    // Replay the fold independently of the prover: verify each step proof, check
    // that consecutive steps chain on the ledger root, and rebuild the history
    // accumulator from the step public inputs alone.
    Commitment rolling_history = expected_public_inputs.initial_state.history_accumulator;
    Commitment last_ledger_root = expected_public_inputs.initial_state.ledger_root;

    for (size_t idx = 0; idx < proof.step_proofs.size(); ++idx) {
        const BatchProof& step_proof = proof.step_proofs[idx];

        auto step_res = SingleBatchVerifier::verify(step_proof.public_inputs, step_proof);
        if (step_res.is_err()) {
            return DomainError{DomainErrorKind::OrderValidationFailed,
                               "step_" + std::to_string(idx) + "_verify: " +
                                   step_res.error_message()};
        }

        // Pre-state continuity. This is the check that stops a session from
        // splicing in a step whose starting ledger is not where the previous
        // step ended.
        if (!(step_proof.public_inputs.pre_state_root == last_ledger_root)) {
            return DomainError{DomainErrorKind::OrderValidationFailed,
                               "step_" + std::to_string(idx) + "_pre_state_continuity"};
        }

        std::vector<uint8_t> accum_payload;
        accum_payload.reserve(32 + 32 + 8 + 8);
        accum_payload.insert(accum_payload.end(), rolling_history.bytes.begin(),
                             rolling_history.bytes.end());
        accum_payload.insert(accum_payload.end(),
                             step_proof.public_inputs.batch_header_hash.bytes.begin(),
                             step_proof.public_inputs.batch_header_hash.bytes.end());
        append_le64(accum_payload, step_proof.public_inputs.clearing_price);
        append_le64(accum_payload, step_proof.public_inputs.cleared_volume);

        rolling_history = Sha256Scheme::commit_raw_bytes(accum_payload);
        last_ledger_root = step_proof.public_inputs.post_state_root;
    }

    if (!(last_ledger_root == expected_public_inputs.final_state.ledger_root)) {
        return DomainError{DomainErrorKind::OrderValidationFailed,
                           "PublicInputMismatch: final_ledger_root"};
    }

    if (!(rolling_history == expected_public_inputs.final_state.history_accumulator)) {
        return DomainError{DomainErrorKind::OrderValidationFailed,
                           "PublicInputMismatch: final_history_accumulator"};
    }

    return ok;
}

} // namespace faircross
