#pragma once

#include <cstdint>
#include <vector>
#include <span>
#include <string_view>
#include <algorithm>
#include <array>
#include "faircross/commitments/commitment.hpp"

namespace faircross {

class Sha256 {
public:
    Sha256();
    void update(const uint8_t* data, size_t length);
    void update(std::span<const uint8_t> data);
    Commitment finalize();

    static Commitment hash(const uint8_t* data, size_t length);
    static Commitment hash(std::span<const uint8_t> data);

private:
    uint32_t state_[8];
    uint64_t bit_len_;
    uint8_t buffer_[64];
    size_t buf_len_;

    void transform(const uint8_t* block);
};

/// SHA-256 based commitment scheme with strict domain separation.
///
/// The leaf, internal-node, and empty-node prefixes are load-bearing, not
/// decoration. Hashing leaves and internal nodes in the same domain admits a
/// second-preimage attack on the Merkle tree: a 64-byte leaf preimage can be
/// reinterpreted as a concatenated node pair, letting an attacker present an
/// internal node as though it were a committed order. These constants and
/// their ordering are pinned two ways: by the frozen digests in
/// `test_golden_batch_header_commitment`, and by the independently derived
/// reference in `tests/conformance/reference_commitments.hpp`.
struct Sha256Scheme {
    using Digest = Commitment;

    static constexpr std::string_view LEAF_PREFIX = "FC_LEAF_";
    static constexpr std::string_view NODE_PREFIX = "FC_NODE_";
    static constexpr std::string_view EMPTY_PREFIX = "FC_EMPTY_NODE";

    static Commitment commit_raw_bytes(const uint8_t* data, size_t len) {
        Sha256 hasher;
        hasher.update(reinterpret_cast<const uint8_t*>(LEAF_PREFIX.data()), LEAF_PREFIX.size());
        hasher.update(data, len);
        return hasher.finalize();
    }

    static Commitment commit_raw_bytes(std::span<const uint8_t> data) {
        return commit_raw_bytes(data.data(), data.size());
    }

    static Commitment combine_nodes(const Commitment& left, const Commitment& right) {
        Sha256 hasher;
        hasher.update(reinterpret_cast<const uint8_t*>(NODE_PREFIX.data()), NODE_PREFIX.size());
        hasher.update(left.bytes.data(), left.bytes.size());
        hasher.update(right.bytes.data(), right.bytes.size());
        return hasher.finalize();
    }

    static Commitment empty_node() {
        Sha256 hasher;
        hasher.update(reinterpret_cast<const uint8_t*>(EMPTY_PREFIX.data()), EMPTY_PREFIX.size());
        return hasher.finalize();
    }
};

} // namespace faircross
