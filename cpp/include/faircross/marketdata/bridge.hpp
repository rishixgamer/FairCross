#pragma once

#include <optional>
#include "faircross/domain/oracle.hpp"
#include "faircross/marketdata/book.hpp"

namespace faircross {

class OracleBridge {
public:
    static std::optional<ReferencePriceSnapshot> snapshot_from_book(
        const OrderBook& book,
        uint32_t oracle_id,
        InstrumentId instrument_id,
        uint64_t timestamp_nanos,
        uint64_t sequence
    ) {
        auto mid = book.mid_price();
        if (!mid.has_value()) return std::nullopt;

        return ReferencePriceSnapshot(
            oracle_id,
            instrument_id,
            mid.value(),
            timestamp_nanos,
            sequence
        );
    }
};

} // namespace faircross
