#include "faircross/marketdata/parser.hpp"

namespace faircross {

namespace {

inline uint64_t read_u48_be(const uint8_t* bytes) {
    uint64_t val = 0;
    for (int i = 0; i < 6; ++i) {
        val = (val << 8) | bytes[i];
    }
    return val;
}

inline uint16_t read_u16_be(const uint8_t* bytes) {
    return static_cast<uint16_t>((static_cast<uint16_t>(bytes[0]) << 8) | bytes[1]);
}

inline uint32_t read_u32_be(const uint8_t* bytes) {
    return (static_cast<uint32_t>(bytes[0]) << 24) |
           (static_cast<uint32_t>(bytes[1]) << 16) |
           (static_cast<uint32_t>(bytes[2]) << 8) |
           static_cast<uint32_t>(bytes[3]);
}

inline uint64_t read_u64_be(const uint8_t* bytes) {
    uint64_t val = 0;
    for (int i = 0; i < 8; ++i) {
        val = (val << 8) | bytes[i];
    }
    return val;
}

} // namespace

Result<std::pair<ItchMessage, size_t>> ItchStreamParser::parse_message(std::span<const uint8_t> slice) {
    if (slice.size() < 3) {
        return DomainError{DomainErrorKind::OrderValidationFailed, "UnexpectedEof"};
    }

    size_t length = read_u16_be(slice.data());
    size_t total_frame_len = 2 + length;

    if (slice.size() < total_frame_len) {
        return DomainError{DomainErrorKind::OrderValidationFailed, "UnexpectedEof"};
    }

    auto payload = slice.subspan(2, length);
    auto msg_res = parse_payload(payload);
    if (msg_res.is_err()) return msg_res.error_message();

    return std::pair<ItchMessage, size_t>{msg_res.value(), total_frame_len};
}

Result<ItchMessage> ItchStreamParser::parse_payload(std::span<const uint8_t> payload) {
    if (payload.empty()) {
        return DomainError{DomainErrorKind::OrderValidationFailed, "UnexpectedEof"};
    }

    uint8_t tag = payload[0];
    const uint8_t* data = payload.data();
    size_t len = payload.size();

    switch (tag) {
        case 'S': {
            if (len != 12) return DomainError{DomainErrorKind::OrderValidationFailed, "InvalidMessageLength"};
            return ItchMessage{SystemEventMessage{
                read_u16_be(data + 1),
                read_u16_be(data + 3),
                read_u48_be(data + 5),
                static_cast<char>(data[11])
            }};
        }
        case 'R': {
            if (len != 39) return DomainError{DomainErrorKind::OrderValidationFailed, "InvalidMessageLength"};
            std::array<uint8_t, 8> symbol{};
            std::copy(data + 11, data + 19, symbol.begin());
            return ItchMessage{StockDirectoryMessage{
                read_u16_be(data + 1),
                read_u16_be(data + 3),
                read_u48_be(data + 5),
                symbol,
                read_u32_be(data + 21)
            }};
        }
        case 'A': {
            if (len != 36) return DomainError{DomainErrorKind::OrderValidationFailed, "InvalidMessageLength"};
            Side side = (data[19] == 'B') ? Side::Buy : ((data[19] == 'S') ? Side::Sell : Side::Buy);
            if (data[19] != 'B' && data[19] != 'S') {
                return PrimitiveError{PrimitiveErrorKind::InvalidSide, "Invalid side byte in ITCH Add"};
            }
            std::array<uint8_t, 8> symbol{};
            std::copy(data + 24, data + 32, symbol.begin());
            return ItchMessage{AddOrderMessage{
                read_u16_be(data + 1),
                read_u16_be(data + 3),
                read_u48_be(data + 5),
                read_u64_be(data + 11),
                side,
                read_u32_be(data + 20),
                symbol,
                read_u32_be(data + 32),
                std::nullopt
            }};
        }
        case 'F': {
            if (len != 40) return DomainError{DomainErrorKind::OrderValidationFailed, "InvalidMessageLength"};
            Side side = (data[19] == 'B') ? Side::Buy : ((data[19] == 'S') ? Side::Sell : Side::Buy);
            if (data[19] != 'B' && data[19] != 'S') {
                return PrimitiveError{PrimitiveErrorKind::InvalidSide, "Invalid side byte in ITCH Add"};
            }
            std::array<uint8_t, 8> symbol{};
            std::copy(data + 24, data + 32, symbol.begin());
            std::array<uint8_t, 4> attr{};
            std::copy(data + 36, data + 40, attr.begin());
            return ItchMessage{AddOrderMessage{
                read_u16_be(data + 1),
                read_u16_be(data + 3),
                read_u48_be(data + 5),
                read_u64_be(data + 11),
                side,
                read_u32_be(data + 20),
                symbol,
                read_u32_be(data + 32),
                attr
            }};
        }
        case 'E': {
            if (len != 31) return DomainError{DomainErrorKind::OrderValidationFailed, "InvalidMessageLength"};
            return ItchMessage{OrderExecutedMessage{
                read_u16_be(data + 1),
                read_u16_be(data + 3),
                read_u48_be(data + 5),
                read_u64_be(data + 11),
                read_u32_be(data + 19),
                read_u64_be(data + 23)
            }};
        }
        case 'C': {
            if (len != 36) return DomainError{DomainErrorKind::OrderValidationFailed, "InvalidMessageLength"};
            return ItchMessage{OrderExecutedWithPriceMessage{
                read_u16_be(data + 1),
                read_u16_be(data + 3),
                read_u48_be(data + 5),
                read_u64_be(data + 11),
                read_u32_be(data + 19),
                read_u64_be(data + 23),
                data[31] == 'Y',
                read_u32_be(data + 32)
            }};
        }
        case 'X': {
            if (len != 23) return DomainError{DomainErrorKind::OrderValidationFailed, "InvalidMessageLength"};
            return ItchMessage{OrderCancelMessage{
                read_u16_be(data + 1),
                read_u16_be(data + 3),
                read_u48_be(data + 5),
                read_u64_be(data + 11),
                read_u32_be(data + 19)
            }};
        }
        case 'D': {
            if (len != 19) return DomainError{DomainErrorKind::OrderValidationFailed, "InvalidMessageLength"};
            return ItchMessage{OrderDeleteMessage{
                read_u16_be(data + 1),
                read_u16_be(data + 3),
                read_u48_be(data + 5),
                read_u64_be(data + 11)
            }};
        }
        case 'U': {
            if (len != 35) return DomainError{DomainErrorKind::OrderValidationFailed, "InvalidMessageLength"};
            return ItchMessage{OrderReplaceMessage{
                read_u16_be(data + 1),
                read_u16_be(data + 3),
                read_u48_be(data + 5),
                read_u64_be(data + 11),
                read_u64_be(data + 19),
                read_u32_be(data + 27),
                read_u32_be(data + 31)
            }};
        }
        default:
            return DomainError{DomainErrorKind::OrderValidationFailed, "UnknownMessageType"};
    }
}

Result<std::vector<ItchMessage>> ItchStreamParser::parse_stream(std::span<const uint8_t> stream) {
    std::vector<ItchMessage> messages;
    auto remaining = stream;

    while (!remaining.empty()) {
        auto res = parse_message(remaining);
        if (res.is_err()) return res.error_message();
        const auto& [msg, consumed] = res.value();
        messages.push_back(msg);
        remaining = remaining.subspan(consumed);
    }

    return messages;
}

} // namespace faircross
