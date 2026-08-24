#pragma once

#include <cstdint>
#include <vector>
#include "faircross/domain/primitives.hpp"
#include "faircross/domain/encoding.hpp"
#include "faircross/commitments/commitment.hpp"
#include "faircross/commitments/sha256.hpp"

namespace faircross {

/// Canonical metadata header bound to a frequent-batch execution round.
struct BatchHeader {
    uint32_t semantic_version;
    BatchId batch_id;
    InstrumentId instrument_id;
    uint64_t cutoff_timestamp;
    uint32_t order_count;
    Commitment orders_merkle_root;

    BatchHeader(
        uint32_t sem_ver,
        BatchId b_id,
        InstrumentId inst,
        uint64_t cutoff,
        uint32_t count,
        Commitment merkle_root
    ) : semantic_version(sem_ver),
        batch_id(b_id),
        instrument_id(inst),
        cutoff_timestamp(cutoff),
        order_count(count),
        orders_merkle_root(merkle_root) {}

    [[nodiscard]] std::vector<uint8_t> canonical_encode() const {
        std::vector<uint8_t> buf;
        buf.reserve(69);
        buf.insert(buf.end(), domain_tags::BATCH_HEADER_TAG.begin(), domain_tags::BATCH_HEADER_TAG.end());
        buf.push_back(ENCODING_VERSION_V1);
        for (int i = 0; i < 4; ++i) buf.push_back(static_cast<uint8_t>((semantic_version >> (i * 8)) & 0xFF));
        append_le64(buf, batch_id.as_raw());
        append_le64(buf, instrument_id.as_raw());
        append_le64(buf, cutoff_timestamp);
        for (int i = 0; i < 4; ++i) buf.push_back(static_cast<uint8_t>((order_count >> (i * 8)) & 0xFF));
        buf.insert(buf.end(), orders_merkle_root.bytes.begin(), orders_merkle_root.bytes.end());
        return buf;
    }

    auto operator<=>(const BatchHeader&) const = default;
};

inline Commitment compute_batch_commitment(const BatchHeader& header) {
    auto encoded = header.canonical_encode();
    return Sha256Scheme::commit_raw_bytes(encoded);
}

} // namespace faircross
