#pragma once

#include "faircross/proof/statement.hpp"

namespace faircross {

class SingleBatchVerifier {
public:
    /// Validates public-input equality and the exact transparent certificate
    /// format. This does not establish cryptographic proof soundness.
    static Result<Ok> verify(
        const BatchProofPublicInputs& expected_public_inputs,
        const BatchProof& proof
    );
};

} // namespace faircross
