#pragma once

// Multi-batch recursive session proof statement types.

#include <vector>
#include <cstdint>

#include "faircross/proof/statement.hpp"
#include "faircross/proof/recursive/state.hpp"

namespace faircross {

/// One batch transition to be folded into a recursive session.
struct RecursiveStep {
    BatchProofPublicInputs public_inputs;
    BatchProofWitness witness;
    uint64_t timestamp_nanos;
};

/// Boundary public inputs for a session proof: the running state before the
/// first batch and after the last.
struct SessionProofPublicInputs {
    RunningState initial_state;
    RunningState final_state;

    auto operator<=>(const SessionProofPublicInputs&) const = default;
};

/// A complete multi-batch running audit proof certificate.
struct SessionProof {
    uint32_t session_version;
    size_t num_batches;
    SessionProofPublicInputs public_inputs;
    std::vector<BatchProof> step_proofs;
    std::vector<uint8_t> proof_certificate;
};

} // namespace faircross
