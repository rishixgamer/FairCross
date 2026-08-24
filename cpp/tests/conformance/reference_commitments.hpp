#pragma once

// Independent reference implementation of the FairCross commitment scheme.
//
// PURPOSE
// -------
// This is the disagreeing party. It exists so that a defect in the production
// commitment scheme has to be reproduced twice, by two separately derived
// implementations, before it can pass the gate (ADR-017).
//
// DERIVATION
// ----------
// Everything here is derived from the specification in docs/DECISIONS.md, NOT
// from the production headers:
//
//   ADR-009  canonical encodings and domain-separated SHA-256 commitments:
//            field-by-field byte offsets, leaf/node/empty hashing, Merkle
//            construction, odd-layer padding, and the empty-batch case
//   ADR-010  batch header binding to the canonical order Merkle root
//   ADR-011  blinded ledger commitments: root over accounts in ascending
//            AccountId order, and per-account salt derivation
//   ADR-012  oracle snapshot preimage and the absent marker
//
// The production code builds byte buffers by appending fields in order. This
// implementation instead writes each field at its documented absolute offset
// into a fixed-size array, and asserts the total length the ADR states. A
// transposed pair of same-width fields, a dropped field, or an off-by-one would
// change one implementation and not the other.
//
// WHAT IS *NOT* INDEPENDENT
// -------------------------
// SHA-256 itself is shared with the production tree. Reimplementing it would add
// little: it is already pinned against published NIST test vectors by
// `test_sha256_standard_vectors`, and it is not where the defect this harness
// was built to catch actually lived (that was domain separation and tree shape).
// A SHA-256 bug would therefore be invisible to this check. See ADR-017.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "faircross/commitments/commitment.hpp"
#include "faircross/commitments/sha256.hpp"
#include "faircross/domain/order.hpp"
#include "faircross/domain/oracle.hpp"
#include "faircross/domain/ledger.hpp"

namespace faircross::conformance {

// --- Domain separation (ADR-009, ADR-009) --------------------------------
inline constexpr std::string_view LEAF_PREFIX = "FC_LEAF_";
inline constexpr std::string_view NODE_PREFIX = "FC_NODE_";
inline constexpr std::string_view EMPTY_PREFIX = "FC_EMPTY_NODE";
inline constexpr std::string_view ORACLE_ABSENT = "FCOS_ABSENT_V1";
inline constexpr std::string_view ACCOUNT_SALT_TAG = "FC_ACCT_SALT_V1";

/// SHA-256 over a byte range. The one primitive shared with production.
inline Commitment sha256(const uint8_t* data, size_t len) {
    return Sha256::hash(data, len);
}

inline Commitment sha256(std::string_view prefix, const uint8_t* data, size_t len) {
    std::vector<uint8_t> buf;
    buf.reserve(prefix.size() + len);
    buf.insert(buf.end(), prefix.begin(), prefix.end());
    buf.insert(buf.end(), data, data + len);
    return sha256(buf.data(), buf.size());
}

// --- Offset-addressed little-endian field writes -------------------------
// The ADRs express layouts as byte ranges ("bytes 5..13: account_id"). These
// helpers mirror that phrasing directly rather than appending sequentially.

template <size_t N>
void put_u8(std::array<uint8_t, N>& buf, size_t offset, uint8_t value) {
    buf[offset] = value;
}

template <size_t N>
void put_u32_le(std::array<uint8_t, N>& buf, size_t offset, uint32_t value) {
    for (size_t i = 0; i < 4; ++i) buf[offset + i] = static_cast<uint8_t>((value >> (i * 8)) & 0xFF);
}

template <size_t N>
void put_u64_le(std::array<uint8_t, N>& buf, size_t offset, uint64_t value) {
    for (size_t i = 0; i < 8; ++i) buf[offset + i] = static_cast<uint8_t>((value >> (i * 8)) & 0xFF);
}

template <size_t N>
void put_u128_le(std::array<uint8_t, N>& buf, size_t offset, unsigned __int128 value) {
    for (size_t i = 0; i < 16; ++i) {
        buf[offset + i] = static_cast<uint8_t>((value >> (i * 8)) & 0xFF);
    }
}

template <size_t N, size_t M>
void put_bytes(std::array<uint8_t, N>& buf, size_t offset, const std::array<uint8_t, M>& src) {
    for (size_t i = 0; i < M; ++i) buf[offset + i] = src[i];
}

template <size_t N>
void put_tag(std::array<uint8_t, N>& buf, size_t offset, std::string_view tag) {
    for (size_t i = 0; i < tag.size(); ++i) buf[offset + i] = static_cast<uint8_t>(tag[i]);
}

// --- ADR-009: SaltedOrderPreimage, 86 bytes ------------------------------
//   0..4 tag "FCOR" | 4 version | 5..13 id | 13..21 account | 21..29 instrument
//   29 side | 30..38 price | 38..46 qty | 46..54 seq | 54..86 salt
inline std::array<uint8_t, 86> encode_order(const SaltedOrderPreimage& p) {
    std::array<uint8_t, 86> buf{};
    put_tag(buf, 0, "FCOR");
    put_u8(buf, 4, 1);
    put_u64_le(buf, 5, p.order.id.as_raw());
    put_u64_le(buf, 13, p.order.account.as_raw());
    put_u64_le(buf, 21, p.order.instrument.as_raw());
    put_u8(buf, 29, p.order.side == Side::Buy ? 0 : 1);
    put_u64_le(buf, 30, p.order.price.as_raw());
    put_u64_le(buf, 38, p.order.qty.as_raw());
    put_u64_le(buf, 46, p.order.seq);
    put_bytes(buf, 54, p.nonce);
    return buf;
}

// --- ADR-009: SaltedAccountPreimage, 77 bytes ----------------------------
//   0..4 tag "FCAC" | 4 version | 5..13 account | 13..29 cash (128-bit unsigned)
//   29..37 instrument | 37..45 inventory | 45..77 salt
inline std::array<uint8_t, 77> encode_account(AccountId account, Money cash,
                                              InstrumentId instrument, Qty inventory,
                                              const std::array<uint8_t, 32>& salt) {
    std::array<uint8_t, 77> buf{};
    put_tag(buf, 0, "FCAC");
    put_u8(buf, 4, 1);
    put_u64_le(buf, 5, account.as_raw());
    put_u128_le(buf, 13, cash.as_raw());
    put_u64_le(buf, 29, instrument.as_raw());
    put_u64_le(buf, 37, inventory.as_raw());
    put_bytes(buf, 45, salt);
    return buf;
}

// --- ADR-010: BatchHeader, 69 bytes --------------------------------------
//   0..4 tag "FCBH" | 4 version | 5..9 semantic_version | 9..17 batch_id
//   17..25 instrument_id | 25..33 cutoff_timestamp | 33..37 order_count
//   37..69 orders_merkle_root
inline std::array<uint8_t, 69> encode_batch_header(uint32_t semantic_version, uint64_t batch_id,
                                                   uint64_t instrument_id, uint64_t cutoff,
                                                   uint32_t order_count,
                                                   const Commitment& merkle_root) {
    std::array<uint8_t, 69> buf{};
    put_tag(buf, 0, "FCBH");
    put_u8(buf, 4, 1);
    put_u32_le(buf, 5, semantic_version);
    put_u64_le(buf, 9, batch_id);
    put_u64_le(buf, 17, instrument_id);
    put_u64_le(buf, 25, cutoff);
    put_u32_le(buf, 33, order_count);
    put_bytes(buf, 37, merkle_root.bytes);
    return buf;
}

// --- ADR-012: OracleSnapshotPreimage, 57 bytes ---------------------------
//   0..4 tag "FCOS" | 4 version | 5..9 oracle_id | 9..17 instrument_id
//   17..25 reference_price | 25..33 timestamp | 33..41 sequence
//   41..49 max_staleness | 49..57 max_deviation
inline std::array<uint8_t, 57> encode_oracle(const ReferencePriceSnapshot& s,
                                             const ReferencePricePolicy& p) {
    std::array<uint8_t, 57> buf{};
    put_tag(buf, 0, "FCOS");
    put_u8(buf, 4, 1);
    put_u32_le(buf, 5, s.oracle_id);
    put_u64_le(buf, 9, s.instrument_id.as_raw());
    put_u64_le(buf, 17, s.reference_price.as_raw());
    put_u64_le(buf, 25, s.timestamp_nanos);
    put_u64_le(buf, 33, s.sequence);
    put_u64_le(buf, 41, p.max_staleness_nanos);
    put_u64_le(buf, 49, p.max_deviation_ticks);
    return buf;
}

// --- Scheme primitives (ADR-009, ADR-009) --------------------------------
inline Commitment leaf_of(const uint8_t* data, size_t len) {
    return sha256(LEAF_PREFIX, data, len);
}

inline Commitment empty_node() {
    return sha256(reinterpret_cast<const uint8_t*>(EMPTY_PREFIX.data()), EMPTY_PREFIX.size());
}

inline Commitment combine(const Commitment& left, const Commitment& right) {
    std::array<uint8_t, 64> pair{};
    put_bytes(pair, 0, left.bytes);
    put_bytes(pair, 32, right.bytes);
    return sha256(NODE_PREFIX, pair.data(), pair.size());
}

/// ADR-009: pairwise combine; a dangling odd node pairs with `empty_node()`;
/// an empty leaf set evaluates to `empty_node()`.
inline Commitment merkle_root(std::vector<Commitment> layer) {
    if (layer.empty()) return empty_node();
    while (layer.size() > 1) {
        std::vector<Commitment> next;
        next.reserve((layer.size() + 1) / 2);
        for (size_t i = 0; i < layer.size(); i += 2) {
            const Commitment& left = layer[i];
            const Commitment right = (i + 1 < layer.size()) ? layer[i + 1] : empty_node();
            next.push_back(combine(left, right));
        }
        layer = std::move(next);
    }
    return layer.front();
}

// --- Derived commitments -------------------------------------------------
inline Commitment commit_order_ref(const SaltedOrderPreimage& p) {
    const auto encoded = encode_order(p);
    return leaf_of(encoded.data(), encoded.size());
}

inline Commitment batch_merkle_root_ref(const std::vector<SaltedOrderPreimage>& preimages) {
    std::vector<Commitment> leaves;
    leaves.reserve(preimages.size());
    for (const auto& p : preimages) leaves.push_back(commit_order_ref(p));
    return merkle_root(std::move(leaves));
}

inline Commitment batch_header_commitment_ref(uint32_t semantic_version, uint64_t batch_id,
                                              uint64_t instrument_id, uint64_t cutoff,
                                              uint32_t order_count,
                                              const Commitment& merkle_root_value) {
    const auto encoded = encode_batch_header(semantic_version, batch_id, instrument_id, cutoff,
                                             order_count, merkle_root_value);
    return leaf_of(encoded.data(), encoded.size());
}

/// ADR-011: salt = SHA256("FC_ACCT_SALT_V1" || venue_secret[32] || account_id_le[8]).
///
/// Built here as a fixed-size offset-addressed buffer, matching how the ADR
/// states the layout, rather than by appending as production does.
inline std::array<uint8_t, 32> derive_account_salt_ref(
    const std::array<uint8_t, 32>& venue_secret, AccountId account_id) {
    std::array<uint8_t, 55> buf{};  // 15 tag + 32 secret + 8 id
    put_tag(buf, 0, ACCOUNT_SALT_TAG);
    put_bytes(buf, ACCOUNT_SALT_TAG.size(), venue_secret);
    put_u64_le(buf, ACCOUNT_SALT_TAG.size() + 32, account_id.as_raw());
    return sha256(LEAF_PREFIX, buf.data(), buf.size()).bytes;
}

/// ADR-011 with ADR-011: Merkle root over accounts in ascending AccountId
/// order, each leaf carrying that account's blinding salt. An empty ledger
/// commits to `empty_node()`.
inline Commitment ledger_root_ref(const Ledger& ledger, InstrumentId instrument) {
    if (ledger.accounts().empty()) return empty_node();

    // Collect and sort explicitly rather than relying on the container's order,
    // so the ADR's stated ordering is the thing being tested.
    std::vector<AccountId> ids;
    for (const auto& [id, _] : ledger.accounts()) ids.push_back(id);
    std::sort(ids.begin(), ids.end(),
              [](AccountId a, AccountId b) { return a.as_raw() < b.as_raw(); });

    std::vector<Commitment> leaves;
    leaves.reserve(ids.size());
    for (const AccountId id : ids) {
        const AccountState* acc = ledger.get_account(id);
        const auto encoded = encode_account(id, acc->cash(), instrument,
                                            acc->inventory_of(instrument),
                                            acc->commitment_salt());
        leaves.push_back(leaf_of(encoded.data(), encoded.size()));
    }
    return merkle_root(std::move(leaves));
}

/// ADR-012: snapshot bound with its policy; absence commits to a distinct marker.
inline Commitment oracle_commitment_ref(const std::optional<ReferencePriceSnapshot>& snapshot,
                                        const std::optional<ReferencePricePolicy>& policy) {
    if (!snapshot.has_value() || !policy.has_value()) {
        return leaf_of(reinterpret_cast<const uint8_t*>(ORACLE_ABSENT.data()),
                       ORACLE_ABSENT.size());
    }
    const auto encoded = encode_oracle(snapshot.value(), policy.value());
    return leaf_of(encoded.data(), encoded.size());
}

} // namespace faircross::conformance
