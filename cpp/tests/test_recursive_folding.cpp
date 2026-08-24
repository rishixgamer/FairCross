#include "test_framework.hpp"
#include "faircross/engine/auction.hpp"
#include "faircross/engine/allocation.hpp"
#include "faircross/engine/fill.hpp"
#include "faircross/engine/accounting.hpp"
#include "faircross/engine/transition.hpp"
#include "faircross/commitments/batch_commitment.hpp"
#include "faircross/commitments/ledger_commitment.hpp"
#include "faircross/commitments/oracle_commitment.hpp"
#include "faircross/proof/recursive/state.hpp"

using namespace faircross;

TEST_CASE(test_recursive_session_folding_step) {
    InstrumentId inst(1);
    Ledger ledger;
    ledger.insert_account(AccountState(AccountId(1), Money::from_raw(10000)));
    AccountState a2(AccountId(2), Money::zero());
    auto _ = a2.credit_inventory(inst, Qty(50));
    ledger.insert_account(std::move(a2));

    // Genesis must commit to the actual starting ledger, or the first fold
    // fails the pre-state continuity check.
    Commitment genesis_root = compute_ledger_root(ledger, inst);
    RunningState state = RunningState::genesis(genesis_root, inst);

    std::vector<Order> orders = {
        Order{OrderId(1), AccountId(1), inst, Side::Buy, Price(100), Qty(10), 0},
        Order{OrderId(2), AccountId(2), inst, Side::Sell, Price(100), Qty(10), 1},
    };
    std::vector<SaltedOrderPreimage> preimages = {
        SaltedOrderPreimage(orders[0], std::array<uint8_t, 32>{1}),
        SaltedOrderPreimage(orders[1], std::array<uint8_t, 32>{2}),
    };
    Batch batch(BatchId(1), inst, orders);
    // A folded step must carry a genuine state transition: pre-state and
    // post-state cannot be the same ledger while fills are reported.
    auto exec = execute_batch(ledger, batch).value();
    const auto& outcome = exec.clearing_outcome;
    const auto& alloc = exec.allocation;
    const auto& fills = exec.fills;
    auto [merkle_tree, acct] = build_complete_input_accounting(batch, preimages, alloc);

    BatchHeader header(1, batch.batch_id(), inst, 1700000010ULL, 2, merkle_tree.root());
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
        1700000010ULL
    };

    auto fold_res = state.fold_step(pub_inputs, witness, 1700000010ULL);
    REQUIRE(fold_res.is_ok());

    RunningState state1 = fold_res.value();
    REQUIRE_EQ(state1.batch_id.as_raw(), 1);
    REQUIRE_NE(state1.history_accumulator, Commitment::zero());
}

TEST_CASE(test_fold_step_rejects_wrong_pre_state_root) {
    // The session verifier checks continuity between consecutive step
    // proofs, but a single fold performed in isolation previously accepted any
    // claimed starting root.
    InstrumentId inst(1);
    Ledger ledger;
    ledger.insert_account(AccountState(AccountId(1), Money::from_raw(10000)));
    AccountState a2(AccountId(2), Money::zero());
    auto _ = a2.credit_inventory(inst, Qty(50));
    ledger.insert_account(std::move(a2));

    RunningState state = RunningState::genesis(compute_ledger_root(ledger, inst), inst);

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
    BatchHeader header(1, batch.batch_id(), inst, 1700000010ULL, 2, tree.root());

    BatchProofWitness witness{
        ledger, exec.post_state, batch, preimages, exec.allocation, exec.fills, acct,
        std::nullopt, std::nullopt, 1700000010ULL};

    // Honest step folds.
    BatchProofPublicInputs honest{
        compute_ledger_root(ledger, inst),
        compute_ledger_root(exec.post_state, inst),
        compute_batch_commitment(header),
        compute_oracle_snapshot_commitment(std::nullopt, std::nullopt),
        exec.clearing_outcome.clearing_price.value().as_raw(),
        exec.clearing_outcome.executable_volume.as_raw()};
    REQUIRE(state.fold_step(honest, witness, 1700000010ULL).is_ok());

    // A step that is internally consistent but starts from a *different*
    // ledger must not fold onto this running state.
    //
    // Forging only the public root would be caught by the prover's own
    // root-to-witness binding, so this builds a genuinely valid step from an
    // unrelated ledger. Only the continuity check against the running state's
    // ledger_root rejects it.
    Ledger other_ledger;
    other_ledger.insert_account(AccountState(AccountId(1), Money::from_raw(99999)));
    AccountState other2(AccountId(2), Money::zero());
    auto __ = other2.credit_inventory(inst, Qty(77));
    other_ledger.insert_account(std::move(other2));

    auto other_exec = execute_batch(other_ledger, batch).value();
    auto [other_tree, other_acct] =
        build_complete_input_accounting(batch, preimages, other_exec.allocation);
    BatchHeader other_header(1, batch.batch_id(), inst, 1700000010ULL, 2, other_tree.root());

    BatchProofWitness other_witness{other_ledger, other_exec.post_state, batch, preimages,
                                    other_exec.allocation, other_exec.fills, other_acct,
                                    std::nullopt, std::nullopt, 1700000010ULL};
    BatchProofPublicInputs other_inputs{
        compute_ledger_root(other_ledger, inst),
        compute_ledger_root(other_exec.post_state, inst),
        compute_batch_commitment(other_header),
        compute_oracle_snapshot_commitment(std::nullopt, std::nullopt),
        other_exec.clearing_outcome.clearing_price.value().as_raw(),
        other_exec.clearing_outcome.executable_volume.as_raw()};

    // The step proves on its own terms...
    REQUIRE(SingleBatchProver::prove(other_inputs, other_witness).is_ok());
    // ...but does not continue from where this running state ended.
    REQUIRE(state.fold_step(other_inputs, other_witness, 1700000010ULL).is_err());
}

TEST_CASE(test_prover_rejects_state_roots_that_do_not_match_the_witness) {
    // Without root-to-witness binding the published roots are free
    // variables: a prover could attach any digest to a valid transition.
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
    BatchHeader header(1, batch.batch_id(), inst, 1700000010ULL, 2, tree.root());

    BatchProofWitness witness{
        ledger, exec.post_state, batch, preimages, exec.allocation, exec.fills, acct,
        std::nullopt, std::nullopt, 1700000010ULL};

    BatchProofPublicInputs honest{
        compute_ledger_root(ledger, inst),
        compute_ledger_root(exec.post_state, inst),
        compute_batch_commitment(header),
        compute_oracle_snapshot_commitment(std::nullopt, std::nullopt),
        exec.clearing_outcome.clearing_price.value().as_raw(),
        exec.clearing_outcome.executable_volume.as_raw()};
    REQUIRE(SingleBatchProver::prove(honest, witness).is_ok());

    std::array<uint8_t, 32> bogus{};
    bogus.fill(0xDD);

    BatchProofPublicInputs bad_pre = honest;
    bad_pre.pre_state_root = Commitment::from_bytes(bogus);
    REQUIRE(SingleBatchProver::prove(bad_pre, witness).is_err());

    BatchProofPublicInputs bad_post = honest;
    bad_post.post_state_root = Commitment::from_bytes(bogus);
    REQUIRE(SingleBatchProver::prove(bad_post, witness).is_err());

    // Swapping the two roots must also fail: the transition is directional.
    BatchProofPublicInputs swapped = honest;
    swapped.pre_state_root = honest.post_state_root;
    swapped.post_state_root = honest.pre_state_root;
    REQUIRE(SingleBatchProver::prove(swapped, witness).is_err());
}
