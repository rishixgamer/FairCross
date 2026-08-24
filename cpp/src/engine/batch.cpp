#include "faircross/engine/batch.hpp"

namespace faircross {

Batch::Batch(BatchId batch_id, InstrumentId instrument, std::vector<Order> orders)
    : batch_id_(batch_id), instrument_(instrument), orders_(std::move(orders)) {
    std::sort(orders_.begin(), orders_.end(), [](const Order& a, const Order& b) {
        if (a.seq != b.seq) return a.seq < b.seq;
        return a.id.as_raw() < b.id.as_raw();
    });
}

Result<Batch> Batch::create(BatchId batch_id, InstrumentId instrument, std::vector<Order> orders) {
    std::set<uint64_t> seen_ids;

    for (const auto& order : orders) {
        if (order.instrument != instrument) {
            return DomainError{DomainErrorKind::InstrumentMismatch, "Instrument mismatch in batch"};
        }
        if (!is_buy(order.side) && !is_sell(order.side)) {
            return PrimitiveError{PrimitiveErrorKind::InvalidSide,
                                  "Invalid side in batch"};
        }
        if (order.price.as_raw() == 0) {
            return PrimitiveError{PrimitiveErrorKind::ZeroValue, "Price"};
        }
        if (order.qty.is_zero()) {
            return PrimitiveError{PrimitiveErrorKind::ZeroValue, "Qty"};
        }
        if (!seen_ids.insert(order.id.as_raw()).second) {
            return DomainError{DomainErrorKind::DuplicateOrderId, "Duplicate OrderId in batch"};
        }
    }

    return Batch(batch_id, instrument, std::move(orders));
}

} // namespace faircross
