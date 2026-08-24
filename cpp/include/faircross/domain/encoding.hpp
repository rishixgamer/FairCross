#pragma once

#include <cstdint>
#include <vector>
#include <array>
#include <cstring>
#include "faircross/domain/primitives.hpp"
#include "faircross/domain/order.hpp"
#include "faircross/domain/oracle.hpp"
#include "faircross/domain/account.hpp"

namespace faircross {

namespace domain_tags {
    inline constexpr std::array<uint8_t, 4> ORDER_TAG = {'F', 'C', 'O', 'R'};
    inline constexpr std::array<uint8_t, 4> ACCOUNT_TAG = {'F', 'C', 'A', 'C'};
    inline constexpr std::array<uint8_t, 4> BATCH_HEADER_TAG = {'F', 'C', 'B', 'H'};
    inline constexpr std::array<uint8_t, 4> ORACLE_SNAPSHOT_TAG = {'F', 'C', 'O', 'S'};
}

inline constexpr uint8_t ENCODING_VERSION_V1 = 1;

/// Salted account state commitment preimage for a specific instrument.
struct SaltedAccountPreimage {
    AccountId account_id;
    Money cash;
    InstrumentId instrument;
    Qty inventory;
    std::array<uint8_t, 32> salt;

    SaltedAccountPreimage(AccountId acc, Money c, InstrumentId inst, Qty inv, std::array<uint8_t, 32> s)
        : account_id(acc), cash(c), instrument(inst), inventory(inv), salt(s) {}

    auto operator<=>(const SaltedAccountPreimage&) const = default;
};

inline void append_le64(std::vector<uint8_t>& buf, uint64_t val) {
    for (int i = 0; i < 8; ++i) {
        buf.push_back(static_cast<uint8_t>((val >> (i * 8)) & 0xFF));
    }
}

inline void append_le128(std::vector<uint8_t>& buf, Money::RawType val) {
    for (int i = 0; i < 16; ++i) {
        buf.push_back(static_cast<uint8_t>((val >> (i * 8)) & 0xFF));
    }
}

inline std::vector<uint8_t> canonical_encode_order(const SaltedOrderPreimage& preimage) {
    std::vector<uint8_t> buf;
    buf.reserve(86);
    buf.insert(buf.end(), domain_tags::ORDER_TAG.begin(), domain_tags::ORDER_TAG.end());
    buf.push_back(ENCODING_VERSION_V1);
    append_le64(buf, preimage.order.id.as_raw());
    append_le64(buf, preimage.order.account.as_raw());
    append_le64(buf, preimage.order.instrument.as_raw());
    buf.push_back(preimage.order.side == Side::Buy ? 0 : 1);
    append_le64(buf, preimage.order.price.as_raw());
    append_le64(buf, preimage.order.qty.as_raw());
    append_le64(buf, preimage.order.seq);
    buf.insert(buf.end(), preimage.nonce.begin(), preimage.nonce.end());
    return buf;
}

inline std::vector<uint8_t> canonical_encode_account(const SaltedAccountPreimage& preimage) {
    std::vector<uint8_t> buf;
    buf.reserve(77);
    buf.insert(buf.end(), domain_tags::ACCOUNT_TAG.begin(), domain_tags::ACCOUNT_TAG.end());
    buf.push_back(ENCODING_VERSION_V1);
    append_le64(buf, preimage.account_id.as_raw());
    append_le128(buf, preimage.cash.as_raw());
    append_le64(buf, preimage.instrument.as_raw());
    append_le64(buf, preimage.inventory.as_raw());
    buf.insert(buf.end(), preimage.salt.begin(), preimage.salt.end());
    return buf;
}

/// A reference-price snapshot bound together with the policy that governs it.
///
/// The policy is part of the preimage on purpose: a snapshot is only meaningful
/// relative to the staleness bound and price collar it was accepted under.
/// Committing to the snapshot alone would let an operator publish a genuine
/// price while silently relaxing the policy that made it admissible.
struct OracleSnapshotPreimage {
    ReferencePriceSnapshot snapshot;
    ReferencePricePolicy policy;

    OracleSnapshotPreimage(ReferencePriceSnapshot s, ReferencePricePolicy p)
        : snapshot(s), policy(p) {}
};

inline void append_le32(std::vector<uint8_t>& buf, uint32_t val) {
    for (int i = 0; i < 4; ++i) {
        buf.push_back(static_cast<uint8_t>((val >> (i * 8)) & 0xFF));
    }
}

inline std::vector<uint8_t> canonical_encode_oracle_snapshot(
    const OracleSnapshotPreimage& preimage
) {
    std::vector<uint8_t> buf;
    buf.reserve(57);
    buf.insert(buf.end(), domain_tags::ORACLE_SNAPSHOT_TAG.begin(),
               domain_tags::ORACLE_SNAPSHOT_TAG.end());
    buf.push_back(ENCODING_VERSION_V1);
    append_le32(buf, preimage.snapshot.oracle_id);
    append_le64(buf, preimage.snapshot.instrument_id.as_raw());
    append_le64(buf, preimage.snapshot.reference_price.as_raw());
    append_le64(buf, preimage.snapshot.timestamp_nanos);
    append_le64(buf, preimage.snapshot.sequence);
    append_le64(buf, preimage.policy.max_staleness_nanos);
    append_le64(buf, preimage.policy.max_deviation_ticks);
    return buf;
}

} // namespace faircross
