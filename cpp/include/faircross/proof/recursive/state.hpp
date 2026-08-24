#pragma once

#include "faircross/domain/primitives.hpp"
#include "faircross/commitments/commitment.hpp"
#include "faircross/proof/statement.hpp"
#include "faircross/proof/prover.hpp"

namespace faircross {

/// Compact running state vector maintained across recursive batch transitions.
struct RunningState {
    Commitment ledger_root;
    Commitment history_accumulator;
    BatchId batch_id;
    uint64_t timestamp_nanos;
    InstrumentId instrument_id;

    static RunningState genesis(Commitment initial_ledger_root, InstrumentId instrument_id) {
        return RunningState{
            initial_ledger_root,
            Commitment::zero(),
            BatchId(0),
            0,
            instrument_id
        };
    }

    Result<RunningState> fold_step(
        const BatchProofPublicInputs& public_inputs,
        const BatchProofWitness& witness,
        uint64_t step_timestamp_nanos
    ) const;

    auto operator<=>(const RunningState&) const = default;
};

} // namespace faircross
