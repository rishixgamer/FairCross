#include "faircross/engine/fill.hpp"

namespace faircross {

Result<std::vector<Fill>> generate_canonical_fills(
    const Batch& batch,
    const BatchAllocation& allocation
) {
    if (!allocation.clearing_price.has_value() || allocation.target_volume.is_zero()) {
        return std::vector<Fill>{};
    }

    Price cp = allocation.clearing_price.value();
    InstrumentId inst = batch.instrument();

    std::vector<Fill> fills;
    uint64_t fill_id = 0;

    for (const auto& alloc : allocation.allocations) {
        if (!alloc.allocated_qty.is_zero()) {
            auto fill_res = Fill::create(
                fill_id++,
                alloc.order_id,
                alloc.account,
                inst,
                alloc.side,
                cp,
                alloc.allocated_qty
            );
            if (fill_res.is_err()) return fill_res.error_message();
            fills.push_back(fill_res.value());
        }
    }

    return fills;
}

} // namespace faircross
