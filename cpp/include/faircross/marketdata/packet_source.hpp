#pragma once

// Portable packet ingestion abstraction.

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "faircross/marketdata/messages.hpp"
#include "faircross/marketdata/parser.hpp"

namespace faircross {

/// Abstract source of framed ITCH messages.
///
/// The abstraction exists so ingestion can be swapped (in-memory, file, socket)
/// without the replay engine knowing which it is. Only the portable in-memory
/// source is implemented; a kernel-bypass source was evaluated and declined
/// (see ADR notes on AF_XDP in docs/DECISIONS.md).
class PacketStreamSource {
public:
    virtual ~PacketStreamSource() = default;

    /// Reads and parses the next message, or nullopt at end of stream.
    /// Returns an error if the stream is malformed.
    virtual Result<std::optional<ItchMessage>> next_message() = 0;
};

/// In-memory packet source over a framed byte buffer. Portable across all
/// supported platforms.
class SlicePacketSource final : public PacketStreamSource {
public:
    explicit SlicePacketSource(std::span<const uint8_t> buffer)
        : buffer_(buffer), cursor_(0) {}

    [[nodiscard]] size_t remaining() const noexcept {
        return cursor_ >= buffer_.size() ? 0 : buffer_.size() - cursor_;
    }

    [[nodiscard]] size_t cursor() const noexcept { return cursor_; }

    Result<std::optional<ItchMessage>> next_message() override {
        if (remaining() == 0) {
            return std::optional<ItchMessage>{std::nullopt};
        }
        auto parsed = ItchStreamParser::parse_message(buffer_.subspan(cursor_));
        if (parsed.is_err()) {
            return parsed.error_message();
        }
        const auto& [msg, consumed] = parsed.value();
        cursor_ += consumed;
        return std::optional<ItchMessage>{msg};
    }

private:
    std::span<const uint8_t> buffer_;
    size_t cursor_;
};

} // namespace faircross
