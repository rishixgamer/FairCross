#include "faircross/simulator/interval.hpp"

namespace faircross {

std::vector<SyntheticBatchBundle> BatchIntervalPartitionEngine::partition_stream(
    const OrderInputStream& stream,
    uint64_t session_start_nanos,
    uint64_t batch_interval_nanos,
    std::optional<size_t> max_batches
) {
    if (stream.submissions.empty() || batch_interval_nanos == 0) {
        return {};
    }

    uint64_t max_arrival = stream.submissions.back().arrival_nanos;
    uint64_t total_time_span = (max_arrival > session_start_nanos) ? (max_arrival - session_start_nanos) : 0;
    size_t calculated_batches = static_cast<size_t>(total_time_span / batch_interval_nanos) + 1;

    size_t num_batches = max_batches.has_value() ? std::min(calculated_batches, max_batches.value()) : calculated_batches;

    std::vector<SyntheticBatchBundle> batches;
    batches.reserve(num_batches);
    size_t submission_idx = 0;

    for (size_t batch_k = 1; batch_k <= num_batches; ++batch_k) {
        uint64_t cutoff_nanos = session_start_nanos + static_cast<uint64_t>(batch_k) * batch_interval_nanos;
        std::vector<Order> batch_orders;
        std::vector<SaltedOrderPreimage> preimages;

        while (submission_idx < stream.submissions.size()) {
            const auto& sub = stream.submissions[submission_idx];
            if (sub.arrival_nanos < cutoff_nanos) {
                batch_orders.push_back(sub.order);
                preimages.push_back(SaltedOrderPreimage(sub.order, sub.salt));
                submission_idx++;
            } else {
                break;
            }
        }

        for (size_t seq = 0; seq < batch_orders.size(); ++seq) {
            batch_orders[seq].seq = seq;
            preimages[seq].order.seq = seq;
        }

        if (!batch_orders.empty()) {
            Batch batch(BatchId(batch_k), stream.instrument_id, std::move(batch_orders));
            batches.push_back(SyntheticBatchBundle{
                std::move(batch),
                std::move(preimages),
                cutoff_nanos
            });
        }
    }

    return batches;
}

} // namespace faircross
