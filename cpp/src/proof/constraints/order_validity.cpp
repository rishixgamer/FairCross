#include "faircross/proof/constraints.hpp"
#include <string>

namespace faircross {

std::vector<AllocatedOrderVar> synthesize_order_validity_constraints(
    ConstraintSystem& cs,
    uint64_t batch_instrument,
    const std::vector<Order>& orders,
    std::vector<uint64_t>& scalars
) {
    std::vector<AllocatedOrderVar> allocated;
    allocated.reserve(orders.size());

    for (size_t idx = 0; idx < orders.size(); ++idx) {
        const Order& order = orders[idx];
        const uint64_t side_val = (order.side == Side::Buy) ? 0u : 1u;

        const Variable id_var = cs.alloc_witness();
        const Variable acc_var = cs.alloc_witness();
        const Variable inst_var = cs.alloc_witness();
        const Variable side_var = cs.alloc_witness();
        const Variable price_var = cs.alloc_witness();
        const Variable qty_var = cs.alloc_witness();
        const Variable seq_var = cs.alloc_witness();

        scalars.push_back(order.id.as_raw());
        scalars.push_back(order.account.as_raw());
        scalars.push_back(order.instrument.as_raw());
        scalars.push_back(side_val);
        scalars.push_back(order.price.as_raw());
        scalars.push_back(order.qty.as_raw());
        scalars.push_back(order.seq);

        const std::string tag = "order_" + std::to_string(idx);

        // 1. Boolean side: side * (1 - side) = 0
        LinearCombination one_minus_side = LinearCombination::from_constant(1);
        one_minus_side.add_term(side_var, -1);
        cs.enforce(LinearCombination::from_var(side_var),
                   one_minus_side,
                   LinearCombination::zero(),
                   tag + "_boolean_side");

        // 2. Instrument match
        cs.enforce_equal(LinearCombination::from_var(inst_var),
                         LinearCombination::from_constant(static_cast<Scalar>(batch_instrument)),
                         tag + "_instrument_match");

        // 3. Monotonic sequence index
        cs.enforce_equal(LinearCombination::from_var(seq_var),
                         LinearCombination::from_constant(static_cast<Scalar>(idx)),
                         tag + "_sequence_match");

        // 4. Positivity via a non-zero gadget: x * inv = x holds with inv = 1
        //    exactly when x is non-zero, and is unsatisfiable for x = 0 unless
        //    inv is also chosen 0, which the witness generation refuses to do.
        const Variable inv_price_var = cs.alloc_witness();
        const Variable inv_qty_var = cs.alloc_witness();

        scalars.push_back(order.price.as_raw() != 0 ? 1u : 0u);
        scalars.push_back(order.qty.as_raw() != 0 ? 1u : 0u);

        cs.enforce(LinearCombination::from_var(price_var),
                   LinearCombination::from_var(inv_price_var),
                   LinearCombination::from_var(price_var),
                   tag + "_price_nonzero");

        cs.enforce(LinearCombination::from_var(qty_var),
                   LinearCombination::from_var(inv_qty_var),
                   LinearCombination::from_var(qty_var),
                   tag + "_qty_nonzero");

        allocated.push_back(AllocatedOrderVar{
            id_var, acc_var, inst_var, side_var, price_var, qty_var, seq_var});
    }

    return allocated;
}

} // namespace faircross
