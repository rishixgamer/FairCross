#pragma once

#include <vector>
#include <set>
#include <algorithm>
#include "faircross/domain/primitives.hpp"
#include "faircross/domain/order.hpp"

namespace faircross {

/// A validated, canonical batch of orders for a single instrument.
class Batch {
public:
    Batch() : batch_id_(0), instrument_(0) {}
    /// Canonicalizes trusted, already-validated orders. Untrusted inputs must
    /// use `create`, which enforces the batch invariants before construction.
    Batch(BatchId batch_id, InstrumentId instrument, std::vector<Order> orders);

    static Result<Batch> create(BatchId batch_id, InstrumentId instrument, std::vector<Order> orders);

    [[nodiscard]] BatchId batch_id() const noexcept { return batch_id_; }
    [[nodiscard]] InstrumentId instrument() const noexcept { return instrument_; }
    [[nodiscard]] const std::vector<Order>& orders() const noexcept { return orders_; }
    [[nodiscard]] bool is_empty() const noexcept { return orders_.empty(); }
    [[nodiscard]] size_t len() const noexcept { return orders_.size(); }

    auto operator<=>(const Batch&) const = default;

private:
    BatchId batch_id_;
    InstrumentId instrument_;
    std::vector<Order> orders_;
};

} // namespace faircross
