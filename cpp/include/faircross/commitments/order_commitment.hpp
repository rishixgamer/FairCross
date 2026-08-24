#pragma once

#include <array>

#include "faircross/domain/order.hpp"
#include "faircross/domain/encoding.hpp"
#include "faircross/commitments/sha256.hpp"

namespace faircross {

/// Computes the salted order commitment digest: H(canonical_encode(preimage)).
inline Commitment commit_order(const SaltedOrderPreimage& preimage) {
    auto encoded = canonical_encode_order(preimage);
    return Sha256Scheme::commit_raw_bytes(encoded);
}

inline Commitment commit_order_fields(const Order& order, const std::array<uint8_t, 32>& nonce) {
    SaltedOrderPreimage preimage(order, nonce);
    return commit_order(preimage);
}

inline bool verify_order_commitment(const Commitment& claimed, const SaltedOrderPreimage& preimage) {
    return commit_order(preimage) == claimed;
}

} // namespace faircross
