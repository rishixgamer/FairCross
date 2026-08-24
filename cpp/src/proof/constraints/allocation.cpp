#include "faircross/proof/constraints.hpp"
#include "faircross/engine/partition.hpp"
#include <string>

namespace faircross {

Result<Ok> synthesize_allocation_constraints(
    ConstraintSystem& cs,
    const Batch& batch,
    Price clearing_price,
    Qty cleared_volume,
    const BatchAllocation& allocation,
    std::vector<uint64_t>& scalars
) {
    // 1. The claimed allocation must equal the canonical allocator's output.
    //    This is what makes preferential allocation unprovable.
    auto honest_res = allocate_batch(batch, std::optional<Price>(clearing_price), cleared_volume);
    if (honest_res.is_err()) {
        return DomainError{DomainErrorKind::OrderValidationFailed,
                           "InvalidWitness: engine failed to allocate batch"};
    }
    if (!(allocation == honest_res.value())) {
        return DomainError{DomainErrorKind::OrderValidationFailed,
                           "deterministic_allocation_rule_match"};
    }

    const Variable vol_pub_var = cs.alloc_public();

    LinearCombination total_buy_lc = LinearCombination::zero();
    LinearCombination total_sell_lc = LinearCombination::zero();

    for (const OrderAllocation& order_alloc : allocation.allocations) {
        const Order* order = nullptr;
        for (const auto& o : batch.orders()) {
            if (o.id == order_alloc.order_id) {
                order = &o;
                break;
            }
        }
        if (order == nullptr) {
            return DomainError{DomainErrorKind::OrderValidationFailed,
                               "InvalidWitness: order not in batch"};
        }

        const uint64_t alloc_qty_val = order_alloc.allocated_qty.as_raw();
        const uint64_t order_qty_val = order->qty.as_raw();

        const Variable alloc_qty_var = cs.alloc_witness();
        const Variable order_qty_var = cs.alloc_witness();

        scalars.push_back(alloc_qty_val);
        scalars.push_back(order_qty_val);

        if (order->side == Side::Buy) {
            total_buy_lc.add_term(alloc_qty_var, 1);
        } else {
            total_sell_lc.add_term(alloc_qty_var, 1);
        }

        const std::string id_tag = std::to_string(order->id.as_raw());

        switch (classify_order(*order, clearing_price)) {
            case OrderMoneyness::InTheMoney:
            case OrderMoneyness::AtTheMoney: {
                // Bounded by the submitted quantity: alloc_qty <= order_qty.
                const uint64_t rem_val =
                    order_qty_val > alloc_qty_val ? order_qty_val - alloc_qty_val : 0;
                const Variable rem_var = cs.alloc_witness();
                scalars.push_back(rem_val);

                LinearCombination lc_diff = LinearCombination::from_var(order_qty_var);
                lc_diff.add_term(alloc_qty_var, -1);
                cs.enforce_equal(lc_diff,
                                 LinearCombination::from_var(rem_var),
                                 "order_" + id_tag + "_fill_bound");
                break;
            }
            case OrderMoneyness::OutOfTheMoney: {
                // Out-of-the-money orders must be strictly unfilled.
                cs.enforce_equal(LinearCombination::from_var(alloc_qty_var),
                                 LinearCombination::zero(),
                                 "otm_order_" + id_tag + "_zero_fill");
                break;
            }
        }
    }

    cs.enforce_equal(total_buy_lc,
                     LinearCombination::from_var(vol_pub_var),
                     "total_buy_allocation_conservation");
    cs.enforce_equal(total_sell_lc,
                     LinearCombination::from_var(vol_pub_var),
                     "total_sell_allocation_conservation");

    return ok;
}

} // namespace faircross
