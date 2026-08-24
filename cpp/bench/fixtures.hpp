#pragma once

// Shared fixture construction for the benchmark harness.
//
// Batch shapes and nonce derivation are fixed here rather than at each call
// site, so the structural columns -- constraint counts, proof sizes, variable
// counts -- stay stable across runs. Only timing columns should ever move.

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "faircross/domain/ledger.hpp"
#include "faircross/engine/batch.hpp"
#include "faircross/engine/transition.hpp"
#include "faircross/engine/accounting.hpp"
#include "faircross/commitments/batch_commitment.hpp"
#include "faircross/commitments/ledger_commitment.hpp"
#include "faircross/commitments/oracle_commitment.hpp"
#include "faircross/proof/prover.hpp"
#include "faircross/proof/constraints.hpp"

namespace faircross::bench {

/// Order shape for the proof-scaling benchmark.
///
/// The price ladder matters: a flat price collapses the candidate-price set and
/// produces a different constraint count, which would make a rerun look like a
/// regression when only the inputs differed.
inline size_t scaling_account_count(size_t batch_size) {
    return std::max<size_t>(batch_size / 2, 2);
}

inline std::vector<Order> scaling_orders(size_t batch_size, InstrumentId inst) {
    const size_t num_accounts = scaling_account_count(batch_size);
    std::vector<Order> orders;
    orders.reserve(batch_size);
    for (size_t i = 0; i < batch_size; ++i) {
        const bool is_buy = (i % 2 == 0);
        const uint64_t price =
            is_buy ? 100 + (static_cast<uint64_t>(i) % 5) : 96 + (static_cast<uint64_t>(i) % 5);
        orders.push_back(Order{
            OrderId(static_cast<uint64_t>(i) + 1),
            AccountId(static_cast<uint64_t>(i % num_accounts) + 1),
            inst,
            is_buy ? Side::Buy : Side::Sell,
            Price(price),
            Qty(10 + (static_cast<uint64_t>(i) % 10)),
            static_cast<uint64_t>(i),
        });
    }
    return orders;
}

/// Genesis ledger for the scaling fixture: one account per two orders, each
/// funded identically.
inline Ledger funded_ledger(InstrumentId inst, size_t batch_size = 4) {
    Ledger ledger;
    const size_t num_accounts = scaling_account_count(batch_size);
    for (size_t i = 1; i <= num_accounts; ++i) {
        AccountState acc(AccountId(static_cast<uint64_t>(i)), Money::from_raw(100'000));
        (void)acc.credit_inventory(inst, Qty(10'000));
        ledger.insert_account(std::move(acc));
    }
    return ledger;
}

inline std::vector<SaltedOrderPreimage> preimages_for(const Batch& batch) {
    std::vector<SaltedOrderPreimage> preimages;
    preimages.reserve(batch.orders().size());
    for (size_t i = 0; i < batch.orders().size(); ++i) {
        std::array<uint8_t, 32> nonce{};
        nonce.fill(static_cast<uint8_t>(static_cast<uint8_t>(i) + 1));
        preimages.emplace_back(batch.orders()[i], nonce);
    }
    return preimages;
}

/// A complete, provable single-batch fixture.
struct ProofFixture {
    BatchProofPublicInputs public_inputs;
    BatchProofWitness witness;
    Ledger post_state;
};

inline ProofFixture build_proof_fixture(size_t batch_size,
                                        const Ledger& pre_state,
                                        uint64_t batch_seq = 1) {
    const InstrumentId inst(1);
    Batch batch(BatchId(batch_seq), inst, scaling_orders(batch_size, inst));
    auto preimages = preimages_for(batch);

    auto exec = execute_batch(pre_state, batch).value();
    auto [tree, accounting] = build_complete_input_accounting(batch, preimages, exec.allocation);

    BatchHeader header(1, batch.batch_id(), inst, 1'700'000'000ULL + batch_seq * 10,
                       static_cast<uint32_t>(batch.len()), tree.root());

    BatchProofPublicInputs public_inputs{
        compute_ledger_root(pre_state, inst),
        compute_ledger_root(exec.post_state, inst),
        compute_batch_commitment(header),
        compute_oracle_snapshot_commitment(std::nullopt, std::nullopt),
        exec.clearing_outcome.clearing_price.has_value()
            ? exec.clearing_outcome.clearing_price->as_raw()
            : 0,
        exec.clearing_outcome.executable_volume.as_raw()};

    BatchProofWitness witness{pre_state,       exec.post_state, batch,
                              preimages,       exec.allocation, exec.fills,
                              accounting,      std::nullopt,    std::nullopt,
                              1'700'000'000ULL + batch_seq * 10};

    return ProofFixture{public_inputs, witness, exec.post_state};
}

/// Extracts the constraint count from a proof certificate.
inline uint64_t constraints_of(const BatchProof& proof) {
    const std::string cert(proof.proof_bytes.begin(), proof.proof_bytes.end());
    const std::string marker = "constraints=";
    const size_t start = cert.find(marker) + marker.size();
    const size_t stop = cert.find(':', start);
    return static_cast<uint64_t>(std::stoull(cert.substr(start, stop - start)));
}

/// Replays the prover's constraint synthesis to recover the full system's
/// variable counts, which the proof certificate does not carry.
///
/// The order must track `SingleBatchProver::prove`; a mismatch would report
/// counts for a system the prover never built.
struct SystemCounts {
    uint64_t constraints;
    uint64_t public_variables;
    uint64_t witness_variables;
};

inline SystemCounts synthesize_full_system(const ProofFixture& fx) {
    const BatchProofWitness& wit = fx.witness;
    std::vector<Commitment> leaves;
    leaves.reserve(wit.preimages.size());
    for (const auto& p : wit.preimages) leaves.push_back(commit_order(p));
    const MerkleTree tree(leaves);

    ConstraintSystem cs;
    std::vector<uint64_t> scalars;

    synthesize_order_validity_constraints(cs, wit.batch.instrument().as_raw(),
                                          wit.batch.orders(), scalars);
    (void)synthesize_complete_input_accounting_constraints(
        cs, wit.batch, wit.preimages, tree, wit.accounting, wit.allocation, scalars);

    std::optional<Price> cp = std::nullopt;
    if (fx.public_inputs.clearing_price > 0) cp = Price(fx.public_inputs.clearing_price);
    const Qty vol = Qty::from_raw(fx.public_inputs.cleared_volume);

    (void)synthesize_clearing_optimality_constraints(cs, wit.batch, cp, vol, scalars);
    if (cp.has_value()) {
        (void)synthesize_allocation_constraints(cs, wit.batch, cp.value(), vol, wit.allocation,
                                                scalars);
        (void)synthesize_fill_bounds_constraints(cs, cp.value(), wit.batch.orders(), wit.fills,
                                                 scalars);
    }
    (void)synthesize_conservation_constraints(cs, wit.pre_state, wit.post_state, wit.fills,
                                              wit.batch.instrument(), scalars);

    return SystemCounts{cs.num_constraints(), cs.num_public(), cs.num_witness()};
}

/// Order shape for the clearing-optimality benchmark. Its price ladder is
/// wider than the scaling fixture's, which changes the candidate-price count and
/// therefore the constraint count: the two benchmarks are not interchangeable.
inline std::vector<Order> optimality_orders(size_t batch_size, InstrumentId inst) {
    std::vector<Order> orders;
    orders.reserve(batch_size);
    for (size_t i = 0; i < batch_size; ++i) {
        const bool is_buy = (i % 2 == 0);
        const uint64_t price =
            is_buy ? 100 + (static_cast<uint64_t>(i) % 10) : 95 + (static_cast<uint64_t>(i) % 10);
        orders.push_back(Order{
            OrderId(static_cast<uint64_t>(i) + 1),
            AccountId(is_buy ? 1 : 2),
            inst,
            is_buy ? Side::Buy : Side::Sell,
            Price(price),
            Qty(10 + (static_cast<uint64_t>(i) % 5)),
            static_cast<uint64_t>(i),
        });
    }
    return orders;
}

/// Two crossing orders at one price, the minimal shape a fold step accepts.
inline std::vector<Order> ivc_step_orders(uint64_t batch_seq, uint64_t price, uint64_t qty,
                                          InstrumentId inst) {
    return {
        Order{OrderId(batch_seq * 2 - 1), AccountId(1), inst, Side::Buy, Price(price), Qty(qty), 0},
        Order{OrderId(batch_seq * 2), AccountId(2), inst, Side::Sell, Price(price), Qty(qty), 1},
    };
}

} // namespace faircross::bench
