#include "faircross/marketdata/replay.hpp"
#include "faircross/marketdata/packet_source.hpp"

namespace faircross {

Result<BboQuote> ItchReplayEngine::step(const ItchMessage& msg) {
    auto applied = book_.apply_message(msg);
    if (applied.is_err()) {
        return applied.error_message();
    }

    BboQuote quote{
        book_.best_bid(),
        book_.best_ask(),
        book_.mid_price(),
        book_.last_timestamp_nanos()
    };
    bbo_history_.push_back(quote);
    return quote;
}

Result<Ok> ItchReplayEngine::replay_messages(const std::vector<ItchMessage>& messages) {
    for (const auto& msg : messages) {
        auto res = step(msg);
        if (res.is_err()) {
            return res.error_message();
        }
    }
    return ok;
}

Result<Ok> ItchReplayEngine::replay_stream(std::span<const uint8_t> stream) {
    // Driven through the packet source abstraction rather than parse_stream, so
    // the ingestion path the engine uses is the one the abstraction describes.
    SlicePacketSource source(stream);
    for (;;) {
        auto next = source.next_message();
        if (next.is_err()) {
            return next.error_message();
        }
        if (!next.value().has_value()) {
            return ok;
        }
        auto res = step(next.value().value());
        if (res.is_err()) {
            return res.error_message();
        }
    }
}

std::optional<ReferencePriceSnapshot> ItchReplayEngine::current_reference_price_snapshot(
    uint32_t oracle_id,
    InstrumentId instrument_id,
    uint64_t sequence
) const {
    return OracleBridge::snapshot_from_book(
        book_, oracle_id, instrument_id, book_.last_timestamp_nanos(), sequence);
}

} // namespace faircross
