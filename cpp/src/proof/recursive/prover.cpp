#include "faircross/proof/recursive/prover.hpp"
#include "faircross/proof/prover.hpp"
#include <sstream>

namespace faircross {

Result<SessionProof> RecursiveSessionProver::prove_session(
    const RunningState& initial_state,
    const std::vector<RecursiveStep>& steps
) {
    if (steps.empty()) {
        return DomainError{DomainErrorKind::OrderValidationFailed,
                           "InvalidWitness: cannot prove empty session"};
    }

    RunningState current_state = initial_state;
    std::vector<BatchProof> step_proofs;
    step_proofs.reserve(steps.size());

    for (size_t idx = 0; idx < steps.size(); ++idx) {
        const RecursiveStep& step = steps[idx];

        // 1. Fold the running state, which verifies the transition and the
        //    batch/instrument/timestamp continuity of this step.
        auto folded = current_state.fold_step(step.public_inputs, step.witness,
                                              step.timestamp_nanos);
        if (folded.is_err()) {
            return DomainError{DomainErrorKind::OrderValidationFailed,
                               "step_" + std::to_string(idx) + "_fold: " + folded.error_message()};
        }
        current_state = folded.value();

        // 2. Generate the single-batch proof for this step.
        auto proof = SingleBatchProver::prove(step.public_inputs, step.witness);
        if (proof.is_err()) {
            return DomainError{DomainErrorKind::OrderValidationFailed,
                               "step_" + std::to_string(idx) + "_prove: " + proof.error_message()};
        }
        step_proofs.push_back(proof.value());
    }

    SessionProofPublicInputs public_inputs{initial_state, current_state};

    std::ostringstream oss;
    oss << "FC-SESSION-PROOF-V1:batches=" << steps.size()
        << ":history_acc=" << public_inputs.final_state.history_accumulator.to_hex()
        << ":final_batch=" << public_inputs.final_state.batch_id.as_raw();
    const std::string cert = oss.str();

    return SessionProof{
        1,
        steps.size(),
        std::move(public_inputs),
        std::move(step_proofs),
        std::vector<uint8_t>(cert.begin(), cert.end())
    };
}

} // namespace faircross
