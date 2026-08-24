#pragma once

#include <vector>
#include <string>
#include <optional>
#include "faircross/domain/ledger.hpp"
#include "faircross/domain/order.hpp"
#include "faircross/engine/batch.hpp"
#include "faircross/adversary/mutator.hpp"

namespace faircross {

struct AttackEvaluationResult {
    std::string attack_name;
    std::string description;
    bool is_honest_control;
    bool passed_verification;
    std::optional<std::string> rejection_reason;
    bool expected_behavior_met;
};

struct AttackMatrixReport {
    uint64_t batch_id;
    uint64_t instrument_id;
    size_t total_evaluated;
    size_t total_attacks_rejected;
    bool all_invariants_held;
    std::vector<AttackEvaluationResult> results;
};

AttackMatrixReport run_attack_matrix(
    const Ledger& pre_state,
    const Batch& batch,
    const std::vector<SaltedOrderPreimage>& preimages
);

} // namespace faircross
