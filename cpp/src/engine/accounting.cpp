#include "faircross/engine/accounting.hpp"

namespace faircross {

std::pair<MerkleTree, CompleteInputAccounting> build_complete_input_accounting(
    [[maybe_unused]] const Batch& batch,
    const std::vector<SaltedOrderPreimage>& preimages,
    const BatchAllocation& allocation
) {
    std::vector<Commitment> leaves;
    leaves.reserve(preimages.size());
    for (const auto& p : preimages) {
        leaves.push_back(commit_order(p));
    }

    MerkleTree tree(leaves);

    std::map<uint64_t, Qty> alloc_map;
    for (const auto& a : allocation.allocations) {
        alloc_map[a.order_id.as_raw()] = a.allocated_qty;
    }

    std::vector<OrderAccountingRecord> records;
    records.reserve(preimages.size());
    size_t total_fully = 0;
    size_t total_partially = 0;
    size_t total_unfilled = 0;

    for (size_t idx = 0; idx < preimages.size(); ++idx) {
        const auto& order = preimages[idx].order;
        auto it = alloc_map.find(order.id.as_raw());
        Qty allocated = (it != alloc_map.end()) ? it->second : Qty::zero();

        OrderDisposition disposition = OrderDisposition::unfilled();
        if (allocated == order.qty) {
            total_fully++;
            disposition = OrderDisposition::fully_executed(allocated);
        } else if (!allocated.is_zero() && allocated.as_raw() < order.qty.as_raw()) {
            total_partially++;
            Qty unfilled(order.qty.as_raw() - allocated.as_raw());
            disposition = OrderDisposition::partially_executed(allocated, unfilled);
        } else {
            total_unfilled++;
            disposition = OrderDisposition::unfilled();
        }

        records.push_back(OrderAccountingRecord{
            idx,
            order.id,
            leaves[idx],
            disposition
        });
    }

    CompleteInputAccounting manifest{
        std::move(records),
        preimages.size(),
        total_fully,
        total_partially,
        total_unfilled,
        0,
        0
    };

    return {std::move(tree), std::move(manifest)};
}

Result<Ok> verify_complete_input_accounting(
    const Batch& batch,
    const MerkleTree& merkle_tree,
    const CompleteInputAccounting& accounting,
    const BatchAllocation& allocation
) {
    size_t n = batch.len();
    if (merkle_tree.leaf_count() != n) {
        return DomainError{DomainErrorKind::OrderValidationFailed, "MerkleLeafCountMismatch"};
    }
    if (accounting.records.size() != n || accounting.total_committed != n) {
        return DomainError{DomainErrorKind::OrderValidationFailed, "TotalCountMismatch"};
    }

    std::set<size_t> seen_indices;
    std::set<uint64_t> seen_order_ids;

    std::map<uint64_t, const Order*> order_map;
    for (const auto& o : batch.orders()) {
        order_map[o.id.as_raw()] = &o;
    }

    std::map<uint64_t, Qty> alloc_map;
    for (const auto& a : allocation.allocations) {
        alloc_map[a.order_id.as_raw()] = a.allocated_qty;
    }

    Commitment root = merkle_tree.root();

    for (const auto& record : accounting.records) {
        if (!seen_indices.insert(record.leaf_index).second) {
            return DomainError{DomainErrorKind::OrderValidationFailed, "DuplicateLeaf"};
        }

        if (!seen_order_ids.insert(record.order_id.as_raw()).second) {
            return DomainError{DomainErrorKind::OrderValidationFailed, "DuplicateOrderId"};
        }

        auto proof_opt = merkle_tree.generate_proof(record.leaf_index);
        if (!proof_opt.has_value()) {
            return DomainError{DomainErrorKind::OrderValidationFailed, "OmittedLeaf"};
        }

        if (!proof_opt->verify(root, record.commitment)) {
            return DomainError{DomainErrorKind::OrderValidationFailed, "MerkleInclusionFailed"};
        }

        auto it = order_map.find(record.order_id.as_raw());
        if (it == order_map.end()) {
            return DomainError{DomainErrorKind::OrderValidationFailed, "DispositionMismatch: order not in batch"};
        }
        const Order* order = it->second;

        auto alloc_it = alloc_map.find(record.order_id.as_raw());
        Qty allocated = (alloc_it != alloc_map.end()) ? alloc_it->second : Qty::zero();

        OrderDisposition expected_disp = OrderDisposition::unfilled();
        if (allocated == order->qty) {
            expected_disp = OrderDisposition::fully_executed(allocated);
        } else if (!allocated.is_zero() && allocated.as_raw() < order->qty.as_raw()) {
            Qty unfilled(order->qty.as_raw() - allocated.as_raw());
            expected_disp = OrderDisposition::partially_executed(allocated, unfilled);
        } else {
            expected_disp = OrderDisposition::unfilled();
        }

        if (record.disposition != expected_disp) {
            return DomainError{DomainErrorKind::OrderValidationFailed, "DispositionMismatch"};
        }
    }

    for (size_t idx = 0; idx < n; ++idx) {
        if (seen_indices.find(idx) == seen_indices.end()) {
            return DomainError{DomainErrorKind::OrderValidationFailed, "OmittedLeaf"};
        }
    }

    return ok;
}

} // namespace faircross
