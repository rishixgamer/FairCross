// Differential conformance: production commitment scheme vs an independently
// derived reference implementation.
//
// The reference in tests/conformance/reference_commitments.hpp is derived from
// the ADR specification rather than from the production headers, so a defect
// must be reproduced in two separate derivations to pass.
//
// Comparison is randomized rather than fixed-vector: the Merkle
// domain-separation defect that motivated this harness survived every
// fixed-input self-consistency test in the C++ suite. Random inputs across many
// shapes, including odd leaf counts, are what make the padding path reachable.

#include "test_framework.hpp"
#include "property.hpp"
#include "conformance/reference_commitments.hpp"

#include "faircross/commitments/order_commitment.hpp"
#include "faircross/commitments/batch_commitment.hpp"
#include "faircross/commitments/ledger_commitment.hpp"
#include "faircross/commitments/oracle_commitment.hpp"
#include "faircross/commitments/merkle.hpp"

#include <sstream>

using namespace faircross;
namespace ref = faircross::conformance;
namespace pt = faircross::property;

namespace {

void require_same(const Commitment& production, const Commitment& reference,
                  const std::string& what) {
    if (!(production == reference)) {
        std::ostringstream oss;
        oss << "CONFORMANCE MISMATCH [" << what << "] production=" << production.to_hex()
            << " reference=" << reference.to_hex();
        throw faircross::test::TestFailure(oss.str());
    }
}

std::array<uint8_t, 32> salt_from(pt::Rng& rng) {
    std::array<uint8_t, 32> salt{};
    for (size_t i = 0; i < salt.size(); ++i) {
        salt[i] = static_cast<uint8_t>(rng.in_range(0, 255));
    }
    return salt;
}

} // namespace

TEST_CASE(conformance_scheme_primitives) {
    // The three domain separators, checked individually so a regression names
    // the constant that drifted.
    const std::vector<uint8_t> empty;
    require_same(Sha256Scheme::commit_raw_bytes(empty),
                 ref::leaf_of(nullptr, 0), "leaf(empty)");

    const std::string abc = "abc";
    const std::vector<uint8_t> abc_bytes(abc.begin(), abc.end());
    require_same(Sha256Scheme::commit_raw_bytes(abc_bytes),
                 ref::leaf_of(abc_bytes.data(), abc_bytes.size()), "leaf(abc)");

    require_same(Sha256Scheme::empty_node(), ref::empty_node(), "empty_node");

    std::array<uint8_t, 32> a{}, b{};
    a.fill(0x11);
    b.fill(0xEE);
    const Commitment ca = Commitment::from_bytes(a);
    const Commitment cb = Commitment::from_bytes(b);
    require_same(Sha256Scheme::combine_nodes(ca, cb), ref::combine(ca, cb), "combine(a,b)");
    // Operand order is part of the definition.
    require_same(Sha256Scheme::combine_nodes(cb, ca), ref::combine(cb, ca), "combine(b,a)");
}

TEST_CASE(conformance_order_commitments_randomized) {
    pt::Rng rng(0x0C0FFEE1);
    for (size_t i = 0; i < 2000; ++i) {
        const Order order{
            OrderId(rng.next_u64()),
            AccountId(rng.next_u64()),
            InstrumentId(rng.next_u64()),
            rng.coin() ? Side::Buy : Side::Sell,
            Price(rng.next_u64()),
            Qty(rng.next_u64()),
            rng.next_u64(),
        };
        const SaltedOrderPreimage preimage(order, salt_from(rng));
        require_same(commit_order(preimage), ref::commit_order_ref(preimage),
                     "order[" + std::to_string(i) + "]");
    }
}

TEST_CASE(conformance_merkle_roots_across_leaf_counts) {
    // 0..33 leaves covers both parities at every depth, so the odd-node padding
    // path is reachable rather than incidental.
    pt::Rng rng(0x5EEDBEEF);
    for (size_t count = 0; count <= 33; ++count) {
        std::vector<SaltedOrderPreimage> preimages;
        std::vector<Commitment> leaves;
        for (size_t i = 0; i < count; ++i) {
            const Order order{
                OrderId(rng.next_u64()),  AccountId(rng.next_u64()),
                InstrumentId(rng.next_u64()), rng.coin() ? Side::Buy : Side::Sell,
                Price(rng.next_u64()),    Qty(rng.next_u64()),
                rng.next_u64(),
            };
            const SaltedOrderPreimage p(order, salt_from(rng));
            preimages.push_back(p);
            leaves.push_back(commit_order(p));
        }
        const Commitment production = MerkleTree(leaves).root();
        require_same(production, ref::batch_merkle_root_ref(preimages),
                     "merkle_root[" + std::to_string(count) + " leaves]");
    }
}

TEST_CASE(conformance_batch_header_commitments_randomized) {
    pt::Rng rng(0xBA7C4EAD);
    for (size_t i = 0; i < 500; ++i) {
        std::array<uint8_t, 32> root_bytes = salt_from(rng);
        const Commitment root = Commitment::from_bytes(root_bytes);

        const auto semantic = static_cast<uint32_t>(rng.next_u64());
        const uint64_t batch_id = rng.next_u64();
        const uint64_t instrument = rng.next_u64();
        const uint64_t cutoff = rng.next_u64();
        const auto count = static_cast<uint32_t>(rng.next_u64());

        BatchHeader header(semantic, BatchId(batch_id), InstrumentId(instrument), cutoff, count,
                           root);
        require_same(compute_batch_commitment(header),
                     ref::batch_header_commitment_ref(semantic, batch_id, instrument, cutoff,
                                                      count, root),
                     "batch_header[" + std::to_string(i) + "]");
    }
}

TEST_CASE(conformance_ledger_roots_randomized) {
    pt::Rng rng(0x1EDbEE7);
    const InstrumentId inst(1);

    // Empty ledger first: the case a zero digest would silently satisfy.
    require_same(compute_ledger_root(Ledger(), inst), ref::ledger_root_ref(Ledger(), inst),
                 "ledger[empty]");

    for (size_t trial = 0; trial < 300; ++trial) {
        Ledger ledger;
        const auto accounts = static_cast<size_t>(rng.in_range(1, 9));
        for (size_t i = 0; i < accounts; ++i) {
            AccountState acc(AccountId(rng.in_range(1, 1000)),
                             Money::from_raw(static_cast<Money::RawType>(rng.next_u64())));
            const uint64_t inv = rng.in_range(0, 10000);
            if (inv > 0) {
                const auto res = acc.credit_inventory(inst, Qty(inv));
                REQUIRE(res.is_ok());
            }
            ledger.insert_account(std::move(acc));
        }
        require_same(compute_ledger_root(ledger, inst), ref::ledger_root_ref(ledger, inst),
                     "ledger[" + std::to_string(trial) + "]");
    }
}

TEST_CASE(conformance_oracle_commitments_randomized) {
    pt::Rng rng(0x0AC1E900);

    require_same(compute_oracle_snapshot_commitment(std::nullopt, std::nullopt),
                 ref::oracle_commitment_ref(std::nullopt, std::nullopt), "oracle[absent]");

    for (size_t i = 0; i < 500; ++i) {
        const ReferencePriceSnapshot snap(static_cast<uint32_t>(rng.next_u64()),
                                          InstrumentId(rng.next_u64()), Price(rng.next_u64()),
                                          rng.next_u64(), rng.next_u64());
        const ReferencePricePolicy policy(rng.next_u64(), rng.next_u64());
        require_same(compute_oracle_snapshot_commitment(snap, policy),
                     ref::oracle_commitment_ref(snap, policy),
                     "oracle[" + std::to_string(i) + "]");
    }
}

TEST_CASE(conformance_blinded_ledger_roots_and_salt_derivation) {
    // ADR-011. The reference derives the salt from the ADR formula independently;
    // a drift in either derivation shows up here.
    pt::Rng rng(0xB11ADE7);
    const InstrumentId inst(1);

    for (size_t trial = 0; trial < 200; ++trial) {
        std::array<uint8_t, 32> secret{};
        for (auto& b : secret) b = static_cast<uint8_t>(rng.in_range(0, 255));

        Ledger ledger;
        const auto accounts = static_cast<size_t>(rng.in_range(1, 7));
        for (size_t i = 0; i < accounts; ++i) {
            AccountState acc(AccountId(rng.in_range(1, 500)),
                             Money::from_raw(static_cast<Money::RawType>(rng.next_u64())));
            const uint64_t inv = rng.in_range(0, 5000);
            if (inv > 0) REQUIRE(acc.credit_inventory(inst, Qty(inv)).is_ok());
            ledger.insert_account(std::move(acc));
        }

        blind_ledger(ledger, secret);
        REQUIRE(ledger.is_fully_blinded());

        // Salt derivation must agree field for field.
        for (const auto& [id, acc] : ledger.accounts()) {
            if (!(acc.commitment_salt() == ref::derive_account_salt_ref(secret, id))) {
                throw faircross::test::TestFailure(
                    "salt derivation differs for account " + std::to_string(id.as_raw()));
            }
        }

        require_same(compute_ledger_root(ledger, inst), ref::ledger_root_ref(ledger, inst),
                     "blinded_ledger[" + std::to_string(trial) + "]");
    }
}

TEST_CASE(blinding_hides_balances_and_preserves_binding) {
    // A published root does not reveal the balance split while still committing
    // to every field.
    const InstrumentId inst(1);
    auto build = [&](uint64_t c1, uint64_t i1, uint64_t c2, uint64_t i2) {
        Ledger l;
        AccountState a(AccountId(1), Money::from_raw(c1));
        if (i1) (void)a.credit_inventory(inst, Qty(i1));
        AccountState b(AccountId(2), Money::from_raw(c2));
        if (i2) (void)b.credit_inventory(inst, Qty(i2));
        l.insert_account(std::move(a));
        l.insert_account(std::move(b));
        return l;
    };

    const std::array<uint8_t, 32> s1 = [] { std::array<uint8_t, 32> s{}; s.fill(0x11); return s; }();
    const std::array<uint8_t, 32> s2 = [] { std::array<uint8_t, 32> s{}; s.fill(0x22); return s; }();

    Ledger unblinded = build(7431, 58, 2569, 142);
    const Commitment plain_root = compute_ledger_root(unblinded, inst);
    REQUIRE(!unblinded.is_fully_blinded());

    Ledger a = build(7431, 58, 2569, 142);
    blind_ledger(a, s1);
    Ledger b = build(7431, 58, 2569, 142);
    blind_ledger(b, s2);

    // Blinding changes the root, and different secrets diverge.
    REQUIRE_NE(compute_ledger_root(a, inst), plain_root);
    REQUIRE_NE(compute_ledger_root(a, inst), compute_ledger_root(b, inst));

    // Stable across a batch that moves nothing.
    Ledger a_again = build(7431, 58, 2569, 142);
    blind_ledger(a_again, s1);
    REQUIRE(compute_ledger_root(a, inst) == compute_ledger_root(a_again, inst));

    // Hiding must not cost binding: every field still moves the root.
    Ledger moved = build(7432, 58, 2568, 142);
    blind_ledger(moved, s1);
    REQUIRE_NE(compute_ledger_root(a, inst), compute_ledger_root(moved, inst));

    // The search that recovered the zero-salt ledger finds nothing here: the
    // attacker knows the totals but cannot compute a leaf without the secret.
    const Commitment target = compute_ledger_root(a, inst);
    bool recovered = false;
    for (uint64_t c1 = 7400; c1 <= 7460 && !recovered; ++c1) {
        for (uint64_t i1 = 40; i1 <= 80; ++i1) {
            Ledger guess = build(c1, i1, 10000 - c1, 200 - i1);
            if (compute_ledger_root(guess, inst) == target) { recovered = true; break; }
        }
    }
    REQUIRE(!recovered);
}

TEST_CASE(participant_verifies_own_balance_without_the_full_ledger) {
    // ADR-011: an account holder checks their own leaf against the published
    // root using only their authentication path.
    const InstrumentId inst(1);
    std::array<uint8_t, 32> secret{};
    secret.fill(0x77);

    Ledger ledger;
    for (uint64_t i = 1; i <= 5; ++i) {
        AccountState acc(AccountId(i), Money::from_raw(1000 * i));
        (void)acc.credit_inventory(inst, Qty(10 * i));
        ledger.insert_account(std::move(acc));
    }
    blind_ledger(ledger, secret);
    const Commitment root = compute_ledger_root(ledger, inst);

    auto proof = ledger_account_proof(ledger, inst, AccountId(3));
    REQUIRE(proof.has_value());
    REQUIRE(proof->proof.verify(root, proof->leaf));

    // A leaf the participant did not commit to must not verify.
    std::array<uint8_t, 32> bogus{};
    bogus.fill(0xEE);
    REQUIRE(!proof->proof.verify(root, Commitment::from_bytes(bogus)));

    // An absent account has no proof.
    REQUIRE(!ledger_account_proof(ledger, inst, AccountId(99)).has_value());
}
