// Golden-vector regression for FairCross market and proof semantics.
//
// `fixtures/golden/expected/` holds frozen reference values -- clearing
// outcome, allocation, fills, post-state, batch header commitment, and R1CS
// constraint count -- for every fixture in `fixtures/golden/`.
//
// These tests replay the same inputs through the engine and assert exact
// agreement. Nothing here hardcodes an expected value: the expectations live in
// the vector files, so a semantic change surfaces as a failure rather than as a
// stale comment. Note what this does and does not buy: it detects drift from a
// recorded value, not disagreement between two independent implementations.
// See docs/LIMITATIONS.md.

#include "test_framework.hpp"
#include "faircross/util/json.hpp"

#include "faircross/domain/primitives.hpp"
#include "faircross/domain/order.hpp"
#include "faircross/domain/account.hpp"
#include "faircross/domain/ledger.hpp"
#include "faircross/commitments/batch_commitment.hpp"
#include "faircross/commitments/ledger_commitment.hpp"
#include "faircross/commitments/oracle_commitment.hpp"
#include "faircross/commitments/order_commitment.hpp"
#include "faircross/commitments/merkle.hpp"
#include "faircross/engine/batch.hpp"
#include "faircross/engine/transition.hpp"
#include "faircross/engine/accounting.hpp"
#include "faircross/simulator/generator.hpp"
#include "faircross/proof/prover.hpp"
#include "faircross/proof/recursive/verifier.hpp"
#include "faircross/proof/recursive/prover.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace faircross;
namespace tj = faircross::json;

namespace {

// The CLI derives per-order nonces as [index + 1; 32] and stamps a fixed batch
// cutoff. Both constants are part of what the frozen batch header commitments
// were recorded under; changing either invalidates every vector below.
constexpr uint64_t kBatchCutoffTimestamp = 1700000000ULL;
constexpr uint32_t kSemanticVersion = 1;

const std::vector<std::string>& fixture_names() {
    static const std::vector<std::string> names = {
        "01_normal_clearing",
        "02_one_sided_no_crossing",
        "03_partial_fill",
        "04_tie_price_interval",
        "05_pro_rata_remainders",
    };
    return names;
}

// Tests may run from the repository root or from cpp/, and check.sh does the
// latter. Resolve the fixture directory rather than assuming one of them.
std::string repo_root() {
    if (const char* env = std::getenv("FAIRCROSS_ROOT")) {
        return std::string(env);
    }
    for (const char* candidate : {"..", "."}) {
        const std::string probe =
            std::string(candidate) + "/fixtures/golden/expected/01_normal_clearing.json";
        std::ifstream in(probe);
        if (in) return std::string(candidate);
    }
    throw std::runtime_error(
        "cannot locate fixtures/golden/expected; run scripts/verify_golden_vectors.py first");
}

std::string golden_input_path(const std::string& name) {
    return repo_root() + "/fixtures/golden/" + name + ".json";
}

std::string golden_expected_path(const std::string& name) {
    return repo_root() + "/fixtures/golden/expected/" + name + ".json";
}

Side side_from_string(const std::string& s) {
    if (s == "buy") return Side::Buy;
    if (s == "sell") return Side::Sell;
    throw std::runtime_error("unknown side '" + s + "'");
}

std::string side_to_string(Side s) {
    return s == Side::Buy ? "buy" : "sell";
}

// Reports the offending fixture and both values, which a bare REQUIRE_EQ cannot.
template <typename T>
void expect_eq(const T& actual, const T& expected, const std::string& what) {
    if (!(actual == expected)) {
        std::ostringstream oss;
        oss << "GOLDEN MISMATCH [" << what << "] expected=" << expected
            << " actual=" << actual;
        throw faircross::test::TestFailure(oss.str());
    }
}

struct LoadedFixture {
    Ledger pre_state;
    Batch batch;
    std::vector<SaltedOrderPreimage> preimages;
};

LoadedFixture load_input(const std::string& name) {
    const tj::Value doc = tj::parse_file(golden_input_path(name));

    Ledger ledger;
    for (const tj::Value& acc : doc.at("pre_state").array()) {
        AccountState state(AccountId(acc.at("account_id").as_u64()),
                           Money::from_raw(static_cast<Money::RawType>(acc.at("cash").as_u64())));
        for (const auto& [inst_str, qty] : acc.at("inventory").object()) {
            const auto inst = InstrumentId(static_cast<uint64_t>(std::stoull(inst_str)));
            const auto res = state.credit_inventory(inst, Qty(qty.as_u64()));
            REQUIRE(res.is_ok());
        }
        ledger.insert_account(std::move(state));
    }

    const tj::Value& batch_doc = doc.at("batch");
    std::vector<Order> orders;
    for (const tj::Value& o : batch_doc.at("orders").array()) {
        orders.push_back(Order{
            OrderId(o.at("id").as_u64()),
            AccountId(o.at("account").as_u64()),
            InstrumentId(o.at("instrument").as_u64()),
            side_from_string(o.at("side").as_string()),
            Price(o.at("price").as_u64()),
            Qty(o.at("qty").as_u64()),
            o.at("seq").as_u64(),
        });
    }

    Batch batch(BatchId(batch_doc.at("batch_id").as_u64()),
                InstrumentId(batch_doc.at("instrument").as_u64()),
                orders);

    std::vector<SaltedOrderPreimage> preimages;
    preimages.reserve(orders.size());
    for (size_t i = 0; i < orders.size(); ++i) {
        std::array<uint8_t, 32> nonce{};
        nonce.fill(static_cast<uint8_t>(i + 1));
        preimages.emplace_back(orders[i], nonce);
    }

    return LoadedFixture{std::move(ledger), std::move(batch), std::move(preimages)};
}

} // namespace

TEST_CASE(test_golden_market_semantics) {
    for (const std::string& name : fixture_names()) {
        LoadedFixture fx = load_input(name);
        const tj::Value expected = tj::parse_file(golden_expected_path(name));

        auto exec_res = execute_batch(fx.pre_state, fx.batch);
        REQUIRE(exec_res.is_ok());
        const BatchExecutionResult& exec = exec_res.value();

        // Clearing outcome. A null clearing price in the expectation means the
        // book did not cross.
        const tj::Value& cp = expected.at("clearing_price");
        if (cp.is_null()) {
            expect_eq(exec.clearing_outcome.clearing_price.has_value(), false,
                      name + ".clearing_price is None");
        } else {
            REQUIRE(exec.clearing_outcome.clearing_price.has_value());
            expect_eq(exec.clearing_outcome.clearing_price->as_raw(), cp.as_u64(),
                      name + ".clearing_price");
        }
        expect_eq(exec.clearing_outcome.executable_volume.as_raw(),
                  expected.at("executable_volume").as_u64(),
                  name + ".executable_volume");

        // Allocation, including largest-remainder pro-rata and its tie-break.
        const tj::Value& alloc = expected.at("allocation");
        expect_eq(exec.allocation.target_volume.as_raw(),
                  alloc.at("target_volume").as_u64(), name + ".target_volume");
        expect_eq(exec.allocation.total_buy_allocated.as_raw(),
                  alloc.at("total_buy_allocated").as_u64(), name + ".total_buy_allocated");
        expect_eq(exec.allocation.total_sell_allocated.as_raw(),
                  alloc.at("total_sell_allocated").as_u64(), name + ".total_sell_allocated");

        const tj::Array& exp_allocs = alloc.at("allocations").array();
        expect_eq(exec.allocation.allocations.size(), exp_allocs.size(),
                  name + ".allocations.len");
        for (size_t i = 0; i < exp_allocs.size(); ++i) {
            const OrderAllocation& got = exec.allocation.allocations[i];
            const tj::Value& want = exp_allocs[i];
            const std::string tag = name + ".allocations[" + std::to_string(i) + "]";
            expect_eq(got.order_id.as_raw(), want.at("order_id").as_u64(), tag + ".order_id");
            expect_eq(got.account.as_raw(), want.at("account").as_u64(), tag + ".account");
            expect_eq(side_to_string(got.side), want.at("side").as_string(), tag + ".side");
            expect_eq(got.limit_price.as_raw(), want.at("limit_price").as_u64(), tag + ".limit_price");
            expect_eq(got.original_qty.as_raw(), want.at("original_qty").as_u64(), tag + ".original_qty");
            expect_eq(got.allocated_qty.as_raw(), want.at("allocated_qty").as_u64(), tag + ".allocated_qty");
            expect_eq(got.seq, want.at("seq").as_u64(), tag + ".seq");
        }

        // Canonical fills, including consideration arithmetic.
        const tj::Array& exp_fills = expected.at("fills").array();
        expect_eq(exec.fills.size(), exp_fills.size(), name + ".fills.len");
        for (size_t i = 0; i < exp_fills.size(); ++i) {
            const Fill& got = exec.fills[i];
            const tj::Value& want = exp_fills[i];
            const std::string tag = name + ".fills[" + std::to_string(i) + "]";
            expect_eq(got.fill_id, want.at("fill_idx").as_u64(), tag + ".fill_idx");
            expect_eq(got.order_id.as_raw(), want.at("order_id").as_u64(), tag + ".order_id");
            expect_eq(got.account_id.as_raw(), want.at("account_id").as_u64(), tag + ".account_id");
            expect_eq(got.instrument_id.as_raw(), want.at("instrument_id").as_u64(), tag + ".instrument_id");
            expect_eq(side_to_string(got.side), want.at("side").as_string(), tag + ".side");
            expect_eq(got.execution_price.as_raw(), want.at("execution_price").as_u64(), tag + ".execution_price");
            expect_eq(got.fill_qty.as_raw(), want.at("fill_qty").as_u64(), tag + ".fill_qty");
            expect_eq(static_cast<uint64_t>(got.consideration.as_raw()),
                      want.at("consideration").as_u64(), tag + ".consideration");
        }

        // Post-state balances and inventories: the conservation surface.
        const tj::Array& exp_accounts = expected.at("post_state").array();
        expect_eq(exec.post_state.accounts().size(), exp_accounts.size(),
                  name + ".post_state.accounts.len");
        for (const tj::Value& want : exp_accounts) {
            const auto account_id = AccountId(want.at("account_id").as_u64());
            const std::string tag =
                name + ".post_state[" + std::to_string(account_id.as_raw()) + "]";
            const AccountState* got = exec.post_state.get_account(account_id);
            REQUIRE(got != nullptr);
            expect_eq(static_cast<uint64_t>(got->cash().as_raw()),
                      want.at("cash").as_u64(), tag + ".cash");
            for (const auto& [inst_str, qty] : want.at("inventory").object()) {
                const auto inst = InstrumentId(static_cast<uint64_t>(std::stoull(inst_str)));
                expect_eq(got->inventory_of(inst).as_raw(), qty.as_u64(),
                          tag + ".inventory[" + inst_str + "]");
            }
        }
    }
}

TEST_CASE(test_golden_batch_header_commitment) {
    // Pins the full commitment chain in one digest: canonical order encoding
    // -> SHA-256 leaf -> Merkle tree -> canonical batch header encoding
    // -> SHA-256. Any encoding drift between the languages changes this hex.
    for (const std::string& name : fixture_names()) {
        LoadedFixture fx = load_input(name);
        const tj::Value expected = tj::parse_file(golden_expected_path(name));

        auto exec_res = execute_batch(fx.pre_state, fx.batch);
        REQUIRE(exec_res.is_ok());

        auto [merkle_tree, accounting] =
            build_complete_input_accounting(fx.batch, fx.preimages, exec_res.value().allocation);

        BatchHeader header(kSemanticVersion,
                           fx.batch.batch_id(),
                           fx.batch.instrument(),
                           kBatchCutoffTimestamp,
                           static_cast<uint32_t>(fx.batch.len()),
                           merkle_tree.root());

        expect_eq(compute_batch_commitment(header).to_hex(),
                  expected.at("batch_header_hash").as_string(),
                  name + ".batch_header_hash");
    }
}

TEST_CASE(test_golden_r1cs_constraint_counts) {
    // The prover synthesizes six constraint subrelations (order validity,
    // complete-input accounting, clearing optimality, allocation, fill bounds,
    // conservation), so the constraint count grows with batch complexity.
    //
    // The count is observable in the proof certificate, which makes it a cheap
    // detector for an accidentally dropped or duplicated subrelation. Do not
    // silence a failure here by re-freezing the vector; establish first that the
    // new count is the intended one.
    for (const std::string& name : fixture_names()) {
        LoadedFixture fx = load_input(name);
        const tj::Value expected = tj::parse_file(golden_expected_path(name));

        auto exec_res = execute_batch(fx.pre_state, fx.batch);
        REQUIRE(exec_res.is_ok());
        const BatchExecutionResult& exec = exec_res.value();

        auto [merkle_tree, accounting] =
            build_complete_input_accounting(fx.batch, fx.preimages, exec.allocation);

        BatchHeader header(kSemanticVersion,
                           fx.batch.batch_id(),
                           fx.batch.instrument(),
                           kBatchCutoffTimestamp,
                           static_cast<uint32_t>(fx.batch.len()),
                           merkle_tree.root());

        BatchProofPublicInputs public_inputs{
            compute_ledger_root(fx.pre_state, fx.batch.instrument()),
            compute_ledger_root(exec.post_state, fx.batch.instrument()),
            compute_batch_commitment(header),
            compute_oracle_snapshot_commitment(std::nullopt, std::nullopt),
            exec.clearing_outcome.clearing_price.has_value()
                ? exec.clearing_outcome.clearing_price->as_raw()
                : 0,
            exec.clearing_outcome.executable_volume.as_raw(),
        };

        BatchProofWitness witness{
            fx.pre_state,
            exec.post_state,
            fx.batch,
            fx.preimages,
            exec.allocation,
            exec.fills,
            accounting,
            std::nullopt,
            std::nullopt,
            1700000000ULL,
        };

        auto proof_res = SingleBatchProver::prove(public_inputs, witness);
        REQUIRE(proof_res.is_ok());

        const std::string certificate(proof_res.value().proof_bytes.begin(),
                                      proof_res.value().proof_bytes.end());
        const std::string marker = "constraints=";
        const size_t start = certificate.find(marker);
        REQUIRE(start != std::string::npos);
        const size_t value_start = start + marker.size();
        const size_t value_end = certificate.find(':', value_start);
        const uint64_t actual = static_cast<uint64_t>(
            std::stoull(certificate.substr(value_start, value_end - value_start)));

        expect_eq(actual, expected.at("r1cs_constraints").as_u64(),
                  name + ".r1cs_constraints");
    }
}

// ---------------------------------------------------------------------------
// Commitment primitive vectors
//
// The batch-level parity tests above exercise the commitment scheme only
// through whole fixtures, and every golden fixture happens to hold an even
// number of orders. That leaves the odd-leaf Merkle padding path -- and with it
// `empty_node()` -- otherwise uncovered by the even-sized batch fixtures. These tests pin each
// commitment-producing function against a frozen digest in
// `fixtures/golden/expected/commitment_vectors.json`.
// ---------------------------------------------------------------------------

namespace {

const tj::Value& commitment_vectors() {
    static const tj::Value vectors =
        tj::parse_file(repo_root() + "/fixtures/golden/expected/commitment_vectors.json");
    return vectors;
}

void expect_digest(const Commitment& actual, const std::string& key) {
    expect_eq(actual.to_hex(), commitment_vectors().at(key).as_string(), key);
}

// The salt pattern the vectors were recorded under: seed + i*7 with 8-bit wrapping
// wraparound. A salt of one repeated byte would hide an off-by-one in a field
// offset, so the bytes must stay distinct.
std::array<uint8_t, 32> patterned_salt(uint8_t seed) {
    std::array<uint8_t, 32> salt{};
    for (size_t i = 0; i < salt.size(); ++i) {
        const auto step = static_cast<uint8_t>(static_cast<uint8_t>(i) * uint8_t{7});
        salt[i] = static_cast<uint8_t>(seed + step);
    }
    return salt;
}

Commitment commit_account(const SaltedAccountPreimage& preimage) {
    return Sha256Scheme::commit_raw_bytes(canonical_encode_account(preimage));
}

} // namespace

TEST_CASE(test_golden_scheme_domain_separation_vectors) {
    // Pins LEAF_PREFIX, NODE_PREFIX, and EMPTY_PREFIX individually, so a
    // regression names the constant that drifted instead of surfacing as an
    // unattributable root mismatch.
    const std::vector<uint8_t> empty_input;
    expect_digest(Sha256Scheme::commit_raw_bytes(empty_input), "scheme.leaf_empty_input");

    const std::string abc = "abc";
    const std::vector<uint8_t> abc_bytes(abc.begin(), abc.end());
    expect_digest(Sha256Scheme::commit_raw_bytes(abc_bytes), "scheme.leaf_abc");

    expect_digest(Sha256Scheme::empty_node(), "scheme.empty_node");

    const Commitment zeros = Commitment::from_bytes(std::array<uint8_t, 32>{});
    std::array<uint8_t, 32> ff{};
    ff.fill(0xFF);
    const Commitment ones = Commitment::from_bytes(ff);

    // Asserted in both operand orders: a left/right swap in combine_nodes would
    // otherwise survive, since a symmetric check cannot detect it.
    expect_digest(Sha256Scheme::combine_nodes(zeros, ones), "scheme.combine_zero_ff");
    expect_digest(Sha256Scheme::combine_nodes(ones, zeros), "scheme.combine_ff_zero");
}

TEST_CASE(test_golden_order_commitment_vectors) {
    struct Case {
        const char* key;
        Order order;
        uint8_t salt_seed;
    };

    const std::vector<Case> cases = {
        {"order.minimal",
         Order{OrderId(1), AccountId(1), InstrumentId(1), Side::Buy, Price(1), Qty(1), 0},
         0x00},
        {"order.sell_side",
         Order{OrderId(2), AccountId(3), InstrumentId(4), Side::Sell, Price(5), Qty(6), 7},
         0x11},
        {"order.wide_fields",
         Order{OrderId(0x0102030405060708ULL), AccountId(0x1112131415161718ULL),
               InstrumentId(0x2122232425262728ULL), Side::Buy, Price(0x3132333435363738ULL),
               Qty(0x4142434445464748ULL), 0x5152535455565758ULL},
         0x77},
        {"order.max_price_qty",
         Order{OrderId(UINT64_MAX), AccountId(UINT64_MAX), InstrumentId(UINT64_MAX), Side::Sell,
               Price(UINT64_MAX), Qty(UINT64_MAX), UINT64_MAX},
         0xAB},
    };

    for (const Case& c : cases) {
        expect_digest(commit_order(SaltedOrderPreimage(c.order, patterned_salt(c.salt_seed))),
                      c.key);
    }
}

TEST_CASE(test_golden_account_commitment_vectors) {
    // These direct primitive vectors make account-field encoding drift visible.
    // `cash` is 128-bit: the wide and max cases fail loudly if the implementation
    // narrows it or emits the wrong byte count.
    const auto wide_cash =
        (static_cast<Money::RawType>(0x0102030405060708ULL) << 64) |
        static_cast<Money::RawType>(0x090A0B0C0D0E0F10ULL);

    struct Case {
        const char* key;
        SaltedAccountPreimage preimage;
    };

    const std::vector<Case> cases = {
        {"account.simple",
         SaltedAccountPreimage(AccountId(1), Money::from_raw(5000), InstrumentId(1), Qty(10),
                               patterned_salt(0x22))},
        {"account.wide_cash",
         SaltedAccountPreimage(AccountId(0x0A0B0C0D0E0F1011ULL), Money::from_raw(wide_cash),
                               InstrumentId(0x2021222324252627ULL), Qty(0x3031323334353637ULL),
                               patterned_salt(0x33))},
        {"account.max_cash",
         SaltedAccountPreimage(AccountId(UINT64_MAX), Money::from_raw(~Money::RawType(0)),
                               InstrumentId(UINT64_MAX), Qty(UINT64_MAX), patterned_salt(0x44))},
        {"account.zero_inventory",
         SaltedAccountPreimage(AccountId(9), Money::from_raw(0), InstrumentId(2), Qty::zero(),
                               patterned_salt(0x55))},
    };

    for (const Case& c : cases) {
        expect_digest(commit_account(c.preimage), c.key);
    }
}

TEST_CASE(test_golden_merkle_root_vectors_including_odd_leaf_counts) {
    // Counts 1..9 cover both parities at every tree depth. The odd counts are
    // the ones that reach the empty-node padding branch.
    for (size_t count = 1; count <= 9; ++count) {
        std::vector<Commitment> leaves;
        leaves.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            const Order order{
                OrderId(static_cast<uint64_t>(i) + 1),
                AccountId(static_cast<uint64_t>(i) + 100),
                InstrumentId(1),
                (i % 2 == 0) ? Side::Buy : Side::Sell,
                Price(static_cast<uint64_t>(i) + 50),
                Qty(static_cast<uint64_t>(i) + 10),
                static_cast<uint64_t>(i),
            };
            leaves.push_back(
                commit_order(SaltedOrderPreimage(order, patterned_salt(static_cast<uint8_t>(i)))));
        }
        const MerkleTree tree(leaves);
        expect_digest(tree.root(), "merkle.root_" + std::to_string(count) + "_leaves");
    }
}

TEST_CASE(test_golden_batch_header_field_ordering_vector) {
    // Every field holds a distinct value, so transposing two same-width fields
    // (batch_id / instrument_id, or semantic_version / order_count) changes the
    // digest rather than cancelling out.
    const BatchHeader header(
        0x0A0B0C0D,
        BatchId(0x1122334455667788ULL),
        InstrumentId(0x99AABBCCDDEEFF00ULL),
        0x0102030405060708ULL,
        0x12131415,
        Commitment::from_bytes(patterned_salt(0x66)));

    expect_digest(compute_batch_commitment(header), "batch_header.distinct_fields");
}

// ---------------------------------------------------------------------------
// Recursive session vectors
//
// Folds the session manifests under `fixtures/` and asserts the recursive
// prover reaches the frozen final running state. The history accumulator chains
// every step's batch header commitment, clearing price, and cleared volume, so
// any divergence in fold ordering, accumulator payload layout, or step
// semantics changes the final digest.
// ---------------------------------------------------------------------------

namespace {

const tj::Value& session_vectors() {
    static const tj::Value vectors =
        tj::parse_file(repo_root() + "/fixtures/golden/expected/session_vectors.json");
    return vectors;
}

// Mirrors the CLI's `prove-session` path. The nonce scheme, cutoff timestamps,
// and synthetic ledger-root chain must match what the vectors were recorded
// under, or the batch header commitments -- and therefore the accumulator --
// diverge.
SessionProof fold_session_from_manifest(const std::string& name) {
    const tj::Value manifest = tj::parse_file(repo_root() + "/fixtures/" + name + ".json");

    Ledger ledger;
    for (const tj::Value& acc : manifest.at("genesis_ledger").array()) {
        AccountState state(AccountId(acc.at("account_id").as_u64()),
                           Money::from_raw(static_cast<Money::RawType>(acc.at("cash").as_u64())));
        for (const auto& [inst_str, qty] : acc.at("inventory").object()) {
            if (qty.as_u64() == 0) continue;
            const auto res = state.credit_inventory(
                InstrumentId(static_cast<uint64_t>(std::stoull(inst_str))),
                Qty(qty.as_u64()));
            REQUIRE(res.is_ok());
        }
        ledger.insert_account(std::move(state));
    }

    // The CLI blinds the genesis ledger before folding (ADR-011); this replica
    // must use the same venue secret or the ledger roots diverge.
    constexpr std::array<uint8_t, 32> kDemoVenueSecret = {
        'F', 'a', 'i', 'r', 'C', 'r', 'o', 's', 's', '-', 'd', 'e', 'm', 'o', '-', 'v',
        'e', 'n', 'u', 'e', '-', 's', 'e', 'c', 'r', 'e', 't', '-', '0', '0', '0', '1'};
    blind_ledger(ledger, kDemoVenueSecret);

    const tj::Array& batch_docs = manifest.at("batches").array();
    REQUIRE(!batch_docs.empty());
    const auto inst = InstrumentId(batch_docs[0].at("instrument").as_u64());

    const Commitment genesis_root = compute_ledger_root(ledger, inst);
    const RunningState initial_state = RunningState::genesis(genesis_root, inst);

    Commitment current_root = genesis_root;
    std::vector<RecursiveStep> steps;

    for (size_t idx = 0; idx < batch_docs.size(); ++idx) {
        const tj::Value& batch_doc = batch_docs[idx];

        std::vector<Order> orders;
        for (const tj::Value& o : batch_doc.at("orders").array()) {
            orders.push_back(Order{
                OrderId(o.at("id").as_u64()),
                AccountId(o.at("account").as_u64()),
                InstrumentId(o.at("instrument").as_u64()),
                side_from_string(o.at("side").as_string()),
                Price(o.at("price").as_u64()),
                Qty(o.at("qty").as_u64()),
                o.at("seq").as_u64(),
            });
        }
        Batch batch(BatchId(batch_doc.at("batch_id").as_u64()),
                    InstrumentId(batch_doc.at("instrument").as_u64()),
                    orders);

        std::vector<SaltedOrderPreimage> preimages;
        preimages.reserve(orders.size());
        for (size_t o_idx = 0; o_idx < orders.size(); ++o_idx) {
            std::array<uint8_t, 32> nonce{};
            nonce.fill(static_cast<uint8_t>(static_cast<uint8_t>(idx * 10 + o_idx) + 1));
            preimages.emplace_back(orders[o_idx], nonce);
        }

        auto exec_res = execute_batch(ledger, batch);
        REQUIRE(exec_res.is_ok());
        const BatchExecutionResult& exec = exec_res.value();

        auto [merkle_tree, acct] =
            build_complete_input_accounting(batch, preimages, exec.allocation);

        const uint64_t timestamp = 1700000000ULL + static_cast<uint64_t>(idx) * 10;
        BatchHeader header(1, batch.batch_id(), batch.instrument(), timestamp,
                           static_cast<uint32_t>(batch.len()), merkle_tree.root());

        const Commitment pre_root = current_root;
        const Commitment post_root = compute_ledger_root(exec.post_state, batch.instrument());
        current_root = post_root;

        BatchProofPublicInputs pub_inputs{
            pre_root,
            post_root,
            compute_batch_commitment(header),
            compute_oracle_snapshot_commitment(std::nullopt, std::nullopt),
            exec.clearing_outcome.clearing_price.has_value()
                ? exec.clearing_outcome.clearing_price->as_raw()
                : 0,
            exec.clearing_outcome.executable_volume.as_raw()
        };

        BatchProofWitness witness{
            ledger, exec.post_state, batch, preimages,
            exec.allocation, exec.fills, acct, std::nullopt, std::nullopt, timestamp
        };

        steps.push_back(RecursiveStep{pub_inputs, witness, timestamp});
        ledger = exec.post_state;
    }

    auto session = RecursiveSessionProver::prove_session(initial_state, steps);
    REQUIRE(session.is_ok());
    return session.value();
}

} // namespace

TEST_CASE(test_golden_recursive_session) {
    for (const std::string& name : {std::string("sample_session"),
                                    std::string("ten_batch_session")}) {
        const SessionProof session = fold_session_from_manifest(name);
        const tj::Value& want = session_vectors().at(name);

        expect_eq(session.num_batches, static_cast<size_t>(want.at("num_batches").as_u64()),
                  name + ".num_batches");
        expect_eq(session.public_inputs.final_state.batch_id.as_raw(),
                  want.at("final_batch_id").as_u64(), name + ".final_batch_id");

        // The accumulator is the load-bearing value: it folds every step's
        // header commitment, clearing price, and cleared volume in order.
        expect_eq(session.public_inputs.final_state.history_accumulator.to_hex(),
                  want.at("final_history_accumulator").as_string(),
                  name + ".final_history_accumulator");
        expect_eq(session.public_inputs.final_state.ledger_root.to_hex(),
                  want.at("final_ledger_root").as_string(), name + ".final_ledger_root");

        expect_eq(std::string(session.proof_certificate.begin(), session.proof_certificate.end()),
                  want.at("proof_certificate").as_string(), name + ".proof_certificate");

        // Per-step constraint counts, so a session cannot match on the digest
        // while its individual steps prove something different.
        const tj::Array& want_steps = want.at("step_constraints").array();
        expect_eq(session.step_proofs.size(), want_steps.size(), name + ".step_proofs.len");
        for (size_t i = 0; i < want_steps.size(); ++i) {
            const std::string cert(session.step_proofs[i].proof_bytes.begin(),
                                   session.step_proofs[i].proof_bytes.end());
            const size_t start = cert.find("constraints=") + std::string("constraints=").size();
            const size_t stop = cert.find(':', start);
            expect_eq(static_cast<uint64_t>(std::stoull(cert.substr(start, stop - start))),
                      want_steps[i].as_u64(),
                      name + ".step_constraints[" + std::to_string(i) + "]");
        }

        // The session must verify independently of the prover that built it.
        auto verdict = RecursiveSessionVerifier::verify_session(session.public_inputs, session);
        if (verdict.is_err()) {
            throw faircross::test::TestFailure(name + " session verification failed: " +
                                               verdict.error_message());
        }
    }
}

TEST_CASE(test_recursive_session_verifier_rejects_broken_chain) {
    // Pre-state continuity is what stops a session from splicing in a step whose
    // starting ledger is not where the previous step ended. fold_step alone does
    // not check it; the session verifier does.
    SessionProof session = fold_session_from_manifest("sample_session");
    REQUIRE(session.step_proofs.size() >= 2);

    std::array<uint8_t, 32> bogus{};
    bogus.fill(0xDD);
    session.step_proofs[1].public_inputs.pre_state_root = Commitment::from_bytes(bogus);

    auto verdict = RecursiveSessionVerifier::verify_session(session.public_inputs, session);
    REQUIRE(verdict.is_err());

    // A tampered final accumulator must also be rejected.
    SessionProof tampered = fold_session_from_manifest("sample_session");
    tampered.public_inputs.final_state.history_accumulator = Commitment::from_bytes(bogus);
    REQUIRE(RecursiveSessionVerifier::verify_session(tampered.public_inputs, tampered).is_err());
}

TEST_CASE(test_golden_ledger_root_vectors) {
    // Pins the ledger commitment: account ordering, the FCAC leaf
    // encoding, the zero leaf salt, and the empty-ledger case. Each variant
    // differs from the base by exactly one field so a mismatch localises the
    // drift rather than merely reporting that a root moved.
    const InstrumentId inst(1);

    auto build = [&](const std::vector<std::tuple<uint64_t, Money::RawType, uint64_t>>& entries) {
        Ledger ledger;
        for (const auto& [id, cash, inv] : entries) {
            AccountState acc(AccountId(id), Money::from_raw(cash));
            if (inv > 0) {
                const auto res = acc.credit_inventory(inst, Qty(inv));
                REQUIRE(res.is_ok());
            }
            ledger.insert_account(std::move(acc));
        }
        return ledger;
    };

    expect_digest(compute_ledger_root(Ledger(), inst), "ledger.empty");
    expect_digest(compute_ledger_root(build({{1, 10000, 50}}), inst), "ledger.single_account");
    expect_digest(compute_ledger_root(build({{1, 10000, 50}, {2, 0, 500}}), inst),
                  "ledger.two_accounts");
    expect_digest(compute_ledger_root(build({{1, 10000, 50}, {2, 0, 500}, {3, 7, 0}}), inst),
                  "ledger.three_accounts_odd");
    expect_digest(
        compute_ledger_root(build({{0x0A0B0C0D0E0F1011ULL, ~Money::RawType(0), UINT64_MAX}}), inst),
        "ledger.wide_cash");
    expect_digest(compute_ledger_root(build({{1, 10001, 50}, {2, 0, 500}}), inst),
                  "ledger.two_accounts_cash_plus_one");
    expect_digest(compute_ledger_root(build({{1, 10000, 50}, {2, 0, 500}}), InstrumentId(2)),
                  "ledger.two_accounts_instrument_2");
}

TEST_CASE(test_ledger_root_binds_balances) {
    // Roots must commit to every ledger field. Mutating any
    // balance must move the root, and insertion order must not.
    const InstrumentId inst(1);

    Ledger a;
    a.insert_account(AccountState(AccountId(1), Money::from_raw(1000)));
    AccountState a2(AccountId(2), Money::from_raw(2000));
    REQUIRE(a2.credit_inventory(inst, Qty(5)).is_ok());
    a.insert_account(std::move(a2));

    // Same contents, inserted in the opposite order.
    Ledger b;
    AccountState b2(AccountId(2), Money::from_raw(2000));
    REQUIRE(b2.credit_inventory(inst, Qty(5)).is_ok());
    b.insert_account(std::move(b2));
    b.insert_account(AccountState(AccountId(1), Money::from_raw(1000)));

    REQUIRE(compute_ledger_root(a, inst) == compute_ledger_root(b, inst));

    // One unit of cash.
    Ledger cash_moved = a;
    REQUIRE(cash_moved.get_or_create_account(AccountId(1)).credit_cash(Money::from_raw(1)).is_ok());
    REQUIRE_NE(compute_ledger_root(a, inst), compute_ledger_root(cash_moved, inst));

    // One lot of inventory.
    Ledger inv_moved = a;
    REQUIRE(inv_moved.get_or_create_account(AccountId(2)).credit_inventory(inst, Qty(1)).is_ok());
    REQUIRE_NE(compute_ledger_root(a, inst), compute_ledger_root(inv_moved, inst));

    // An extra account.
    Ledger padded = a;
    padded.insert_account(AccountState(AccountId(3), Money::zero()));
    REQUIRE_NE(compute_ledger_root(a, inst), compute_ledger_root(padded, inst));
}

TEST_CASE(test_golden_synthetic_session) {
    // The session is derived from a seeded SplitMix64, so a divergence
    // in the sequence of draws silently produces a different session from the
    // same seed. Digesting every generated preimage in order catches that, along
    // with batch sizing and salt derivation.
    const SyntheticSession session =
        SyntheticOrderFlowSimulator::generate_session(SyntheticFlowConfig{});

    std::vector<uint8_t> payload;
    auto append_le64 = [&payload](uint64_t v) {
        for (size_t i = 0; i < 8; ++i) {
            payload.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
        }
    };

    append_le64(static_cast<uint64_t>(session.batches.size()));
    for (const auto& bundle : session.batches) {
        append_le64(bundle.cutoff_nanos);
        append_le64(static_cast<uint64_t>(bundle.preimages.size()));
        for (const auto& preimage : bundle.preimages) {
            const auto encoded = canonical_encode_order(preimage);
            payload.insert(payload.end(), encoded.begin(), encoded.end());
        }
    }

    expect_digest(Sha256Scheme::commit_raw_bytes(payload), "synthetic.default_session");
}

TEST_CASE(test_golden_oracle_snapshot_vectors) {
    // Pins the FCOS encoding, the absent marker, and the fact that the
    // policy is part of the preimage rather than merely surrounding context.
    const ReferencePriceSnapshot base(7, InstrumentId(1), Price(100), 1700000000ULL, 42);
    const ReferencePricePolicy base_policy(5000000000ULL, 10);

    expect_digest(compute_oracle_snapshot_commitment(std::nullopt, std::nullopt), "oracle.absent");
    expect_digest(compute_oracle_snapshot_commitment(base, base_policy), "oracle.base");

    ReferencePriceSnapshot wide = base;
    wide.oracle_id = 0xFEEDBEEF;
    wide.instrument_id = InstrumentId(0x1122334455667788ULL);
    wide.reference_price = Price(0x0102030405060708ULL);
    wide.timestamp_nanos = 0x0A0B0C0D0E0F1011ULL;
    wide.sequence = UINT64_MAX;
    expect_digest(compute_oracle_snapshot_commitment(wide, base_policy), "oracle.wide_fields");

    ReferencePriceSnapshot shifted = base;
    shifted.timestamp_nanos += 1;
    expect_digest(compute_oracle_snapshot_commitment(shifted, base_policy),
                  "oracle.timestamp_plus_one");

    // Same snapshot, relaxed policy: must not collide with the base.
    expect_digest(
        compute_oracle_snapshot_commitment(
            base, ReferencePricePolicy(base_policy.max_staleness_nanos * 2, 10)),
        "oracle.relaxed_staleness");
    expect_digest(
        compute_oracle_snapshot_commitment(
            base, ReferencePricePolicy(base_policy.max_staleness_nanos, 11)),
        "oracle.wider_collar");

    // The absent marker must not be the zero digest the placeholder used.
    REQUIRE_NE(compute_oracle_snapshot_commitment(std::nullopt, std::nullopt), Commitment::zero());
}
