#pragma once

#include <vector>

#include "faircross/proof/recursive/statement.hpp"

namespace faircross {

/// Folds multiple sequential batch transitions into a native history record.
class RecursiveSessionProver {
public:
    static Result<SessionProof> prove_session(
        const RunningState& initial_state,
        const std::vector<RecursiveStep>& steps
    );
};

} // namespace faircross
