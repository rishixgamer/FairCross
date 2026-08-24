#include "faircross/proof/constraints.hpp"
#include <string>

namespace faircross {

Result<Ok> synthesize_fill_bounds_constraints(
    ConstraintSystem& cs,
    Price clearing_price,
    const std::vector<Order>& orders,
    const std::vector<Fill>& fills,
    std::vector<uint64_t>& scalars
) {
    const uint64_t cp_val = clearing_price.as_raw();

    const Variable cp_var = cs.alloc_witness();
    scalars.push_back(cp_val);

    for (const Fill& fill : fills) {
        const Order* order = nullptr;
        for (const auto& o : orders) {
            if (o.id == fill.order_id) {
                order = &o;
                break;
            }
        }
        if (order == nullptr) {
            return DomainError{DomainErrorKind::OrderValidationFailed,
                               "InvalidWitness: fill refers to unknown order id"};
        }

        const uint64_t fill_qty_val = fill.fill_qty.as_raw();
        const uint64_t order_qty_val = order->qty.as_raw();
        const auto consideration_val = static_cast<uint64_t>(fill.consideration.as_raw());
        const std::string id_tag = std::to_string(order->id.as_raw());

        // 1. No overfill.
        if (fill_qty_val > order_qty_val) {
            return DomainError{DomainErrorKind::OrderValidationFailed,
                               "order_" + id_tag + "_no_overfill"};
        }

        const uint64_t remaining_qty_val = order_qty_val - fill_qty_val;

        const Variable fill_qty_var = cs.alloc_witness();
        const Variable order_qty_var = cs.alloc_witness();
        const Variable rem_qty_var = cs.alloc_witness();
        const Variable consideration_var = cs.alloc_witness();

        scalars.push_back(fill_qty_val);
        scalars.push_back(order_qty_val);
        scalars.push_back(remaining_qty_val);
        scalars.push_back(consideration_val);

        LinearCombination lc_diff = LinearCombination::from_var(order_qty_var);
        lc_diff.add_term(fill_qty_var, -1);
        cs.enforce_equal(lc_diff,
                         LinearCombination::from_var(rem_qty_var),
                         "order_" + id_tag + "_remaining_qty_bound");

        // 2. Consideration: clearing_price * fill_qty = consideration.
        cs.enforce(LinearCombination::from_var(cp_var),
                   LinearCombination::from_var(fill_qty_var),
                   LinearCombination::from_var(consideration_var),
                   "fill_" + std::to_string(fill.fill_id) + "_consideration_product");

        // 3. Limit price compliance, expressed as a non-negative slack.
        const uint64_t limit_val = order->price.as_raw();
        const Variable limit_var = cs.alloc_witness();
        scalars.push_back(limit_val);

        if (order->side == Side::Buy) {
            if (cp_val > limit_val && fill_qty_val > 0) {
                return DomainError{DomainErrorKind::OrderValidationFailed,
                                   "buy_order_" + id_tag + "_limit_price"};
            }
            const uint64_t slack_val = limit_val > cp_val ? limit_val - cp_val : 0;
            const Variable slack_var = cs.alloc_witness();
            scalars.push_back(slack_val);

            LinearCombination lc_limit = LinearCombination::from_var(limit_var);
            lc_limit.add_term(cp_var, -1);
            cs.enforce_equal(lc_limit,
                             LinearCombination::from_var(slack_var),
                             "buy_order_" + id_tag + "_limit_slack");
        } else {
            if (cp_val < limit_val && fill_qty_val > 0) {
                return DomainError{DomainErrorKind::OrderValidationFailed,
                                   "sell_order_" + id_tag + "_limit_price"};
            }
            const uint64_t slack_val = cp_val > limit_val ? cp_val - limit_val : 0;
            const Variable slack_var = cs.alloc_witness();
            scalars.push_back(slack_val);

            LinearCombination lc_limit = LinearCombination::from_var(cp_var);
            lc_limit.add_term(limit_var, -1);
            cs.enforce_equal(lc_limit,
                             LinearCombination::from_var(slack_var),
                             "sell_order_" + id_tag + "_limit_slack");
        }
    }

    return ok;
}

} // namespace faircross
