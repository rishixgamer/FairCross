#pragma once

#include <algorithm>
#include <array>
#include <optional>
#include <tuple>
#include <vector>
#include "faircross/domain/primitives.hpp"
#include "faircross/domain/order.hpp"
#include "faircross/simulator/generator.hpp"

namespace faircross {

struct TimestampedOrderSubmission {
    Order order;
    uint64_t arrival_nanos;
    std::array<uint8_t, 32> salt;
};

struct OrderInputStream {
    InstrumentId instrument_id;
    std::vector<TimestampedOrderSubmission> submissions;

    OrderInputStream(InstrumentId inst, std::vector<TimestampedOrderSubmission> subs)
        : instrument_id(inst), submissions(std::move(subs)) {
        std::sort(submissions.begin(), submissions.end(), [](const TimestampedOrderSubmission& a, const TimestampedOrderSubmission& b) {
            // Arrival time alone is not a total order. Canonicalize every observable
            // field so equal-timestamp input permutations produce the same sequence.
            return std::tie(a.arrival_nanos, a.order.id, a.order.account,
                            a.order.instrument, a.order.side, a.order.price,
                            a.order.qty, a.order.seq, a.salt)
                 < std::tie(b.arrival_nanos, b.order.id, b.order.account,
                            b.order.instrument, b.order.side, b.order.price,
                            b.order.qty, b.order.seq, b.salt);
        });
    }
};

class BatchIntervalPartitionEngine {
public:
    static std::vector<SyntheticBatchBundle> partition_stream(
        const OrderInputStream& stream,
        uint64_t session_start_nanos,
        uint64_t batch_interval_nanos,
        std::optional<size_t> max_batches = std::nullopt
    );
};

} // namespace faircross
