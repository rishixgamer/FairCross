#pragma once

#include <vector>
#include <span>
#include "faircross/domain/error.hpp"
#include "faircross/marketdata/messages.hpp"

namespace faircross {

class ItchStreamParser {
public:
    static Result<std::pair<ItchMessage, size_t>> parse_message(std::span<const uint8_t> slice);
    static Result<ItchMessage> parse_payload(std::span<const uint8_t> payload);
    static Result<std::vector<ItchMessage>> parse_stream(std::span<const uint8_t> stream);
};

} // namespace faircross
