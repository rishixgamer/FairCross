#include "faircross/proof/recursive/state.hpp"
#include "faircross/commitments/sha256.hpp"

namespace faircross {

Result<RunningState> RunningState::fold_step(
    const BatchProofPublicInputs& public_inputs,
    const BatchProofWitness& witness,
    uint64_t step_timestamp_nanos
) const {
    // 1. Monotonic batch sequence check
    uint64_t expected_batch_id = batch_id.as_raw() + 1;
    if (witness.batch.batch_id().as_raw() != expected_batch_id) {
        return DomainError{DomainErrorKind::OrderValidationFailed, "BatchSequenceContinuityFailed"};
    }

    // 2. Instrument continuity check
    if (witness.batch.instrument() != instrument_id) {
        return DomainError{DomainErrorKind::InstrumentMismatch, "InstrumentContinuityFailed"};
    }

    // 3. Timestamp monotonicity check
    if (step_timestamp_nanos < timestamp_nanos) {
        return DomainError{DomainErrorKind::OrderValidationFailed, "TimestampMonotonicityFailed"};
    }

    // 4. Pre-state continuity: the step must start from the ledger this running
    //    state ended at. The session verifier checks this between consecutive
    //    step proofs, but a single fold performed in isolation would otherwise
    //    accept any claimed starting root.
    if (!(public_inputs.pre_state_root == ledger_root)) {
        return DomainError{DomainErrorKind::OrderValidationFailed,
                           "pre_state_root_continuity"};
    }

    // 5. Verify single-batch transition proof for this step
    auto proof_res = SingleBatchProver::prove(public_inputs, witness);
    if (proof_res.is_err()) {
        return proof_res.error_message();
    }

    // 6. Update history accumulator: H_k = Hash(H_{k-1} || header_hash || cp || vol)
    std::vector<uint8_t> accum_payload;
    accum_payload.reserve(32 + 32 + 8 + 8);
    accum_payload.insert(accum_payload.end(), history_accumulator.bytes.begin(), history_accumulator.bytes.end());
    accum_payload.insert(accum_payload.end(), public_inputs.batch_header_hash.bytes.begin(), public_inputs.batch_header_hash.bytes.end());
    append_le64(accum_payload, public_inputs.clearing_price);
    append_le64(accum_payload, public_inputs.cleared_volume);

    Commitment next_history = Sha256Scheme::commit_raw_bytes(accum_payload);

    return RunningState{
        public_inputs.post_state_root,
        next_history,
        witness.batch.batch_id(),
        step_timestamp_nanos,
        instrument_id
    };
}

} // namespace faircross
