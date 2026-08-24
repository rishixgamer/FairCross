#include "test_framework.hpp"
#include "faircross/engine/auction.hpp"
#include "faircross/engine/allocation.hpp"
#include "faircross/engine/fill.hpp"
#include "faircross/engine/accounting.hpp"
#include "faircross/engine/transition.hpp"
#include "faircross/commitments/batch_commitment.hpp"
#include "faircross/commitments/ledger_commitment.hpp"
#include "faircross/commitments/oracle_commitment.hpp"
#include "faircross/proof/r1cs.hpp"
#include "faircross/proof/prover.hpp"
#include "faircross/proof/verifier.hpp"

#include <cstdint>
#include <utility>

using namespace faircross;

TEST_CASE(test_single_batch_proof_prove_and_verify) {
    InstrumentId inst(1);
    Ledger ledger;
    ledger.insert_account(AccountState(AccountId(1), Money::from_raw(10000)));
    AccountState a2(AccountId(2), Money::zero());
    auto _ = a2.credit_inventory(inst, Qty(50));
    ledger.insert_account(std::move(a2));

    std::vector<Order> orders = {
        Order{OrderId(1), AccountId(1), inst, Side::Buy, Price(100), Qty(10), 0},
        Order{OrderId(2), AccountId(2), inst, Side::Sell, Price(100), Qty(10), 1},
    };
    std::vector<SaltedOrderPreimage> preimages = {
        SaltedOrderPreimage(orders[0], std::array<uint8_t, 32>{1}),
        SaltedOrderPreimage(orders[1], std::array<uint8_t, 32>{2}),
    };

    Batch batch(BatchId(1), inst, orders);
    // Execute the batch to obtain a real post-state. Passing the pre-state as
    // the post-state would describe a transition in which the trade settled but
    // no balance moved, which the conservation subrelation rejects.
    auto exec = execute_batch(ledger, batch).value();
    const auto& outcome = exec.clearing_outcome;
    const auto& alloc = exec.allocation;
    const auto& fills = exec.fills;
    auto [merkle_tree, acct] = build_complete_input_accounting(batch, preimages, alloc);
    const uint64_t cutoff = 1700000000ULL;

    BatchHeader header(1, batch.batch_id(), inst, cutoff, 2, merkle_tree.root());
    Commitment header_hash = compute_batch_commitment(header);

    BatchProofPublicInputs pub_inputs{
        compute_ledger_root(ledger, inst),
        compute_ledger_root(exec.post_state, inst),
        header_hash,
        compute_oracle_snapshot_commitment(std::nullopt, std::nullopt),
        outcome.clearing_price.value().as_raw(),
        outcome.executable_volume.as_raw()
    };

    BatchProofWitness witness{
        ledger,
        exec.post_state,
        batch,
        preimages,
        alloc,
        fills,
        acct,
        std::nullopt,
        std::nullopt,
        cutoff
    };

    auto proof_res = SingleBatchProver::prove(pub_inputs, witness);
    REQUIRE(proof_res.is_ok());

    auto verify_res = SingleBatchVerifier::verify(pub_inputs, proof_res.value());
    REQUIRE(verify_res.is_ok());

    // A status suffix by itself used to be accepted as a "proof". The
    // transparent verifier now requires the exact emitted artifact grammar,
    // while remaining explicitly non-cryptographic.
    BatchProof suffix_only{1, pub_inputs,
                           {':', 's', 't', 'a', 't', 'u', 's', '=', 'S', 'A', 'T'}};
    REQUIRE(SingleBatchVerifier::verify(pub_inputs, suffix_only).is_err());

    // The batch-header hash is bound to the witness batch, cutoff, order count,
    // and Merkle root; an arbitrary published digest cannot be substituted.
    BatchProofPublicInputs arbitrary_header = pub_inputs;
    arbitrary_header.batch_header_hash = Commitment::zero();
    REQUIRE(SingleBatchProver::prove(arbitrary_header, witness).is_err());

    // Extra committed inputs are rejected both at the prover boundary and by
    // the standalone complete-input accounting check.
    BatchProofWitness extra_preimage = witness;
    extra_preimage.preimages.push_back(preimages[0]);
    REQUIRE(SingleBatchProver::prove(pub_inputs, extra_preimage).is_err());

    std::vector<Commitment> extra_leaves = merkle_tree.leaves();
    extra_leaves.push_back(commit_order(preimages[0]));
    MerkleTree extra_tree(extra_leaves);
    REQUIRE(verify_complete_input_accounting(batch, extra_tree, acct, alloc).is_err());

    // Accounting vectors may arrive out of leaf order, but their leaf-index,
    // order-id, and commitment bindings remain mandatory.
    BatchProofWitness reordered_accounting = witness;
    std::swap(reordered_accounting.accounting.records[0],
              reordered_accounting.accounting.records[1]);
    REQUIRE(SingleBatchProver::prove(pub_inputs, reordered_accounting).is_ok());

    BatchProofWitness mismatched_preimage = witness;
    mismatched_preimage.preimages[0] = preimages[1];
    REQUIRE(SingleBatchProver::prove(pub_inputs, mismatched_preimage).is_err());

    BatchProofWitness mismatched_accounting = witness;
    mismatched_accounting.accounting.records[0].commitment =
        mismatched_accounting.accounting.records[1].commitment;
    REQUIRE(SingleBatchProver::prove(pub_inputs, mismatched_accounting).is_err());
}

TEST_CASE(test_r1cs_scalar_arithmetic_exceeds_64_bit_range) {
    // The clearing-optimality subrelation emits a constant term of
    // demand * supply, a 64-bit-by-64-bit unsigned product. With 64-bit coefficients this
    // silently overflowed -- undefined behaviour under -fsanitize=undefined,
    // and a wrong constant even where it did not trap.
    const uint64_t big = 4'000'000'000ULL; // 4e9; the square is ~1.6e19 > INT64_MAX
    ConstraintSystem cs;
    const Variable d_var = cs.alloc_witness();
    const Variable s_var = cs.alloc_witness();

    const Scalar product = wrapping_mul(static_cast<Scalar>(big), static_cast<Scalar>(big));
    REQUIRE(product > static_cast<Scalar>(INT64_MAX));

    cs.enforce(LinearCombination::from_var(d_var),
               LinearCombination::from_var(s_var),
               LinearCombination::from_constant(product),
               "wide_product");

    REQUIRE(cs.is_satisfied({}, {big, big}).is_ok());
    // A different multiplicand must not satisfy the same constant.
    REQUIRE(cs.is_satisfied({}, {big, big - 1}).is_err());

    // Overflow wraps rather than trapping; see wrapping_add in r1cs.hpp.
    const Scalar max = static_cast<Scalar>((UScalar{1} << 127) - 1);
    REQUIRE(wrapping_add(max, 1) < 0);
}

TEST_CASE(test_oracle_snapshot_is_bound_to_the_published_statement) {
    // A batch clearing against reference data must publish an
    // oracle_snapshot_hash derived from that data, and the snapshot must satisfy
    // the policy it was committed under.
    InstrumentId inst(1);
    Ledger ledger;
    ledger.insert_account(AccountState(AccountId(1), Money::from_raw(10000)));
    AccountState a2(AccountId(2), Money::zero());
    auto _ = a2.credit_inventory(inst, Qty(50));
    ledger.insert_account(std::move(a2));

    std::vector<Order> orders = {
        Order{OrderId(1), AccountId(1), inst, Side::Buy, Price(100), Qty(10), 0},
        Order{OrderId(2), AccountId(2), inst, Side::Sell, Price(100), Qty(10), 1},
    };
    std::vector<SaltedOrderPreimage> preimages = {
        SaltedOrderPreimage(orders[0], std::array<uint8_t, 32>{1}),
        SaltedOrderPreimage(orders[1], std::array<uint8_t, 32>{2}),
    };
    Batch batch(BatchId(1), inst, orders);
    auto exec = execute_batch(ledger, batch).value();
    auto [tree, acct] = build_complete_input_accounting(batch, preimages, exec.allocation);

    const uint64_t cutoff = 1700000000000000000ULL;
    BatchHeader header(1, batch.batch_id(), inst, cutoff, 2, tree.root());
    const ReferencePricePolicy policy(1000000000ULL, 10);
    const Price cp = exec.clearing_outcome.clearing_price.value();
    const ReferencePriceSnapshot fresh(7, inst, cp, cutoff - 500000000ULL, 1);

    BatchProofPublicInputs pub_inputs{
        compute_ledger_root(ledger, inst),
        compute_ledger_root(exec.post_state, inst),
        compute_batch_commitment(header),
        compute_oracle_snapshot_commitment(fresh, policy),
        cp.as_raw(),
        exec.clearing_outcome.executable_volume.as_raw()};

    BatchProofWitness witness{ledger,          exec.post_state, batch,  preimages,
                              exec.allocation, exec.fills,      acct,   fresh,
                              policy,          cutoff};

    // An honest, fresh snapshot proves.
    REQUIRE(SingleBatchProver::prove(pub_inputs, witness).is_ok());

    // 1. A stale snapshot cannot be proven, even with a matching hash.
    const ReferencePriceSnapshot stale(7, inst, cp, cutoff - 5000000000ULL, 2);
    BatchProofWitness stale_witness = witness;
    stale_witness.oracle_snapshot = stale;
    BatchProofPublicInputs stale_inputs = pub_inputs;
    stale_inputs.oracle_snapshot_hash = compute_oracle_snapshot_commitment(stale, policy);
    REQUIRE(SingleBatchProver::prove(stale_inputs, stale_witness).is_err());

    // 2. Substituting the snapshot while keeping the published hash is rejected.
    BatchProofWitness swapped = witness;
    swapped.oracle_snapshot = ReferencePriceSnapshot(7, inst, cp, cutoff - 400000000ULL, 3);
    REQUIRE(SingleBatchProver::prove(pub_inputs, swapped).is_err());

    // 3. Silently widening the policy is rejected: it is part of the commitment.
    BatchProofWitness relaxed = witness;
    relaxed.oracle_policy = ReferencePricePolicy(policy.max_staleness_nanos * 100, 10);
    REQUIRE(SingleBatchProver::prove(pub_inputs, relaxed).is_err());

    // 4. Claiming no oracle while the published hash says otherwise.
    BatchProofWitness absent = witness;
    absent.oracle_snapshot = std::nullopt;
    absent.oracle_policy = std::nullopt;
    REQUIRE(SingleBatchProver::prove(pub_inputs, absent).is_err());

    // 5. A snapshot for a different instrument is rejected by the plaintext
    //    validator before proof synthesis.
    BatchProofWitness wrong_inst = witness;
    const ReferencePriceSnapshot other_inst(7, InstrumentId(2), cp, cutoff - 500000000ULL, 1);
    wrong_inst.oracle_snapshot = other_inst;
    BatchProofPublicInputs wrong_inputs = pub_inputs;
    wrong_inputs.oracle_snapshot_hash = compute_oracle_snapshot_commitment(other_inst, policy);
    REQUIRE(SingleBatchProver::prove(wrong_inputs, wrong_inst).is_err());
}
