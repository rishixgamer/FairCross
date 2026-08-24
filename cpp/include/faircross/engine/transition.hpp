#pragma once

#include <vector>

#include "faircross/domain/ledger.hpp"
#include "faircross/domain/fill.hpp"
#include "faircross/engine/batch.hpp"
#include "faircross/engine/auction.hpp"
#include "faircross/engine/allocation.hpp"
#include "faircross/engine/fill.hpp"

namespace faircross {

/// Result of executing a batch state transition on a ledger.
struct BatchExecutionResult {
    ClearingOutcome clearing_outcome;
    BatchAllocation allocation;
    std::vector<Fill> fills;
    Ledger post_state;

    auto operator<=>(const BatchExecutionResult&) const = default;
};

/// Applies canonical fills to a ledger, returning the settled post-state.
///
/// Shared by `execute_batch` and by the transition checker, so the ledger a
/// batch produces and the ledger the checker reconstructs come from one
/// definition rather than two that can drift apart.
Result<Ledger> apply_batch_transition(
    const Ledger& pre_state,
    const std::vector<Fill>& fills
);

Result<BatchExecutionResult> execute_batch(
    const Ledger& pre_state,
    const Batch& batch
);

} // namespace faircross
