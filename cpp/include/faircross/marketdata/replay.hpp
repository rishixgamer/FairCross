#pragma once

// Deterministic ITCH stream replay engine.

#include <optional>
#include <span>
#include <vector>

#include "faircross/domain/oracle.hpp"
#include "faircross/marketdata/book.hpp"
#include "faircross/marketdata/bridge.hpp"
#include "faircross/marketdata/messages.hpp"

namespace faircross {

/// A best-bid/best-offer observation taken after applying one message.
struct BboQuote {
    std::optional<Price> best_bid;
    std::optional<Price> best_ask;
    std::optional<Price> mid_price;
    uint64_t timestamp_nanos;

    auto operator<=>(const BboQuote&) const = default;
};

/// Replays ITCH messages against an order book and records the BBO trajectory.
class ItchReplayEngine {
public:
    ItchReplayEngine() = default;

    [[nodiscard]] const OrderBook& book() const noexcept { return book_; }
    [[nodiscard]] const std::vector<BboQuote>& bbo_history() const noexcept {
        return bbo_history_;
    }

    /// Applies one message and records the resulting quote.
    Result<BboQuote> step(const ItchMessage& msg);

    /// Replays parsed messages in order.
    Result<Ok> replay_messages(const std::vector<ItchMessage>& messages);

    /// Replays a raw framed byte stream.
    Result<Ok> replay_stream(std::span<const uint8_t> stream);

    /// Extracts a reference price snapshot from the current book state.
    [[nodiscard]] std::optional<ReferencePriceSnapshot> current_reference_price_snapshot(
        uint32_t oracle_id,
        InstrumentId instrument_id,
        uint64_t sequence
    ) const;

private:
    OrderBook book_;
    std::vector<BboQuote> bbo_history_;
};

} // namespace faircross
