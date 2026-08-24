#pragma once

// Deterministic ledger state commitment.
//
// `pre_state_root` and `post_state_root` are documented in
// docs/PROOF_STATEMENT.md as Merkle roots over account ledger balances.
//
// Each account leaf carries the stable, venue-secret-derived blinding salt from
// ADR-086. This prevents an outside observer from recomputing low-entropy
// balances without the venue secret; it does not hide balances from the venue
// or conceal the account set.

#include <array>
#include <vector>
#include <string_view>
#include <optional>

#include "faircross/domain/ledger.hpp"
#include "faircross/domain/encoding.hpp"
#include "faircross/commitments/merkle.hpp"
#include "faircross/commitments/sha256.hpp"

namespace faircross {

/// Domain separator for account salt derivation (ADR-086).
inline constexpr std::string_view ACCOUNT_SALT_TAG = "FC_ACCT_SALT_V1";

/// Derives an account's blinding salt from the operator's venue secret.
///
/// A pure function of the secret and the account id, so the salt is stable
/// across batches and the operator regenerates it without storing one per
/// account. An observer without the secret cannot compute a leaf even knowing
/// the balance exactly.
inline std::array<uint8_t, 32> derive_account_salt(const std::array<uint8_t, 32>& venue_secret,
                                                   AccountId account_id) {
    std::vector<uint8_t> payload;
    payload.reserve(ACCOUNT_SALT_TAG.size() + 32 + 8);
    payload.insert(payload.end(), ACCOUNT_SALT_TAG.begin(), ACCOUNT_SALT_TAG.end());
    payload.insert(payload.end(), venue_secret.begin(), venue_secret.end());
    append_le64(payload, account_id.as_raw());
    return Sha256Scheme::commit_raw_bytes(payload).bytes;
}

/// Populates every account's blinding salt from the venue secret.
inline void blind_ledger(Ledger& ledger, const std::array<uint8_t, 32>& venue_secret) {
    for (auto& [id, acc] : ledger.accounts_mut()) {
        acc.set_commitment_salt(derive_account_salt(venue_secret, id));
    }
}

/// Computes the ledger commitment root for a single instrument.
///
/// Accounts are committed in ascending AccountId order, which the ledger's
/// ordered map already guarantees, so the root is a pure function of ledger
/// contents and independent of insertion order. An empty ledger commits to
/// `empty_node()` so it is distinguishable from an absent one.
inline Commitment compute_ledger_root(const Ledger& ledger, InstrumentId instrument) {
    if (ledger.accounts().empty()) {
        return Sha256Scheme::empty_node();
    }

    std::vector<Commitment> leaves;
    leaves.reserve(ledger.accounts().size());
    for (const auto& [account_id, state] : ledger.accounts()) {
        const SaltedAccountPreimage preimage(
            account_id,
            state.cash(),
            instrument,
            state.inventory_of(instrument),
            state.commitment_salt());
        leaves.push_back(Sha256Scheme::commit_raw_bytes(canonical_encode_account(preimage)));
    }

    return MerkleTree(std::move(leaves)).root();
}

/// An account's leaf commitment and its authentication path to the ledger root.
///
/// Lets a participant verify their own balance against the published root
/// without being shown any other account's contents.
struct AccountInclusionProof {
    Commitment leaf;
    MerkleProof proof;
};

/// Builds an inclusion proof for one account, or nullopt if it is absent.
inline std::optional<AccountInclusionProof> ledger_account_proof(const Ledger& ledger,
                                                                 InstrumentId instrument,
                                                                 AccountId account_id) {
    std::vector<Commitment> leaves;
    leaves.reserve(ledger.accounts().size());
    std::optional<size_t> index;
    size_t position = 0;
    for (const auto& [id, state] : ledger.accounts()) {
        const SaltedAccountPreimage preimage(id, state.cash(), instrument,
                                             state.inventory_of(instrument),
                                             state.commitment_salt());
        leaves.push_back(Sha256Scheme::commit_raw_bytes(canonical_encode_account(preimage)));
        if (id == account_id) index = position;
        ++position;
    }
    if (!index.has_value()) return std::nullopt;

    const Commitment leaf = leaves[index.value()];
    const MerkleTree tree(std::move(leaves));
    auto proof = tree.generate_proof(index.value());
    if (!proof.has_value()) return std::nullopt;
    return AccountInclusionProof{leaf, proof.value()};
}

} // namespace faircross
