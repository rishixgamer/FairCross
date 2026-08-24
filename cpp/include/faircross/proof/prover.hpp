#pragma once

#include "faircross/proof/statement.hpp"
#include "faircross/proof/r1cs.hpp"

namespace faircross {

class SingleBatchProver {
public:
    static Result<BatchProof> prove(
        const BatchProofPublicInputs& public_inputs,
        const BatchProofWitness& witness
    );
};

} // namespace faircross
