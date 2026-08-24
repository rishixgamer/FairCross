#pragma once

#include <string>
#include "faircross/domain/account.hpp"
#include "faircross/domain/fill.hpp"
#include "faircross/engine/batch.hpp"
#include "faircross/engine/transition.hpp"

namespace faircross {

enum class InvariantViolationKind {
    CashConservation,
    AssetConservation,
    VolumeConservation,
    Overfill,
    LimitPriceViolation,
    IncorrectConsideration,
    NonDeterministicClearing,
    AllocationRuleMismatch,
    AccountStateMismatch,
};

struct InvariantViolation {
    InvariantViolationKind kind;
    std::string message;

    std::string to_string() const { return message; }
};

Result<Ok> verify_transition(
    const Ledger& pre_state,
    const Batch& batch,
    const BatchExecutionResult& proposed
);

} // namespace faircross
