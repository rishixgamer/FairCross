#pragma once

#include <vector>
#include <map>
#include <set>
#include <optional>
#include "faircross/domain/primitives.hpp"
#include "faircross/domain/order.hpp"
#include "faircross/commitments/merkle.hpp"
#include "faircross/commitments/order_commitment.hpp"
#include "faircross/engine/batch.hpp"
#include "faircross/engine/allocation.hpp"

namespace faircross {

enum class DispositionKind {
    FullyExecuted,
    PartiallyExecuted,
    Unfilled,
    Cancelled,
    Expired,
};

struct OrderDisposition {
    DispositionKind kind;
    Qty fill_qty;
    Qty unfilled_qty;

    static OrderDisposition fully_executed(Qty fill_qty) {
        return OrderDisposition{DispositionKind::FullyExecuted, fill_qty, Qty::zero()};
    }

    static OrderDisposition partially_executed(Qty fill_qty, Qty rem) {
        return OrderDisposition{DispositionKind::PartiallyExecuted, fill_qty, rem};
    }

    static OrderDisposition unfilled() {
        return OrderDisposition{DispositionKind::Unfilled, Qty::zero(), Qty::zero()};
    }

    auto operator<=>(const OrderDisposition&) const = default;
};

struct OrderAccountingRecord {
    size_t leaf_index;
    OrderId order_id;
    Commitment commitment;
    OrderDisposition disposition;

    auto operator<=>(const OrderAccountingRecord&) const = default;
};

struct CompleteInputAccounting {
    std::vector<OrderAccountingRecord> records;
    size_t total_committed;
    size_t total_fully_executed;
    size_t total_partially_executed;
    size_t total_unfilled;
    size_t total_cancelled;
    size_t total_expired;

    [[nodiscard]] size_t leaf_count() const noexcept { return records.size(); }

    auto operator<=>(const CompleteInputAccounting&) const = default;
};

std::pair<MerkleTree, CompleteInputAccounting> build_complete_input_accounting(
    const Batch& batch,
    const std::vector<SaltedOrderPreimage>& preimages,
    const BatchAllocation& allocation
);

Result<Ok> verify_complete_input_accounting(
    const Batch& batch,
    const MerkleTree& merkle_tree,
    const CompleteInputAccounting& accounting,
    const BatchAllocation& allocation
);

} // namespace faircross
