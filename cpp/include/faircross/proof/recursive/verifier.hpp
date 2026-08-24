#pragma once


#include "faircross/proof/recursive/statement.hpp"

namespace faircross {

/// Replays transparent step certificates and checks native history continuity.
/// This is not a recursive cryptographic-proof verifier.
class RecursiveSessionVerifier {
public:
    static Result<Ok> verify_session(
        const SessionProofPublicInputs& expected_public_inputs,
        const SessionProof& proof
    );
};

} // namespace faircross
