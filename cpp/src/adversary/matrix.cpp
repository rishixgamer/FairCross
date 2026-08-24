#include "faircross/adversary/matrix.hpp"
#include "faircross/engine/checker.hpp"
#include "faircross/engine/accounting.hpp"
#include "faircross/engine/oracle_validation.hpp"
#include "faircross/commitments/order_commitment.hpp"

namespace faircross {

AttackMatrixReport run_attack_matrix(
    const Ledger& pre_state,
    const Batch& batch,
    const std::vector<SaltedOrderPreimage>& preimages
) {
    std::vector<Commitment> committed_leaves;
    committed_leaves.reserve(preimages.size());
    for (const auto& p : preimages) {
        committed_leaves.push_back(commit_order(p));
    }
    MerkleTree original_merkle_tree(committed_leaves);

    OrderId target_order_id = batch.is_empty() ? OrderId(1) : batch.orders()[0].id;
    AccountId target_account_id = batch.is_empty() ? AccountId(1) : batch.orders()[0].account;
    InstrumentId inst = batch.instrument();

    struct TestCase {
        std::string name;
        std::string desc;
        AdversarialMutation mutation;
        bool expect_pass;
    };

    auto make_mut = [](MutationType t) {
        AdversarialMutation m;
        m.type = t;
        return m;
    };

    auto mut_censor = make_mut(MutationType::CensorOrder);
    mut_censor.leaf_index = 0;

    auto mut_price_inf = make_mut(MutationType::ManipulateClearingPrice);
    mut_price_inf.offset_ticks = 5;

    auto mut_price_def = make_mut(MutationType::ManipulateClearingPrice);
    mut_price_def.offset_ticks = -5;

    auto mut_overfill = make_mut(MutationType::ManipulateAllocation);
    mut_overfill.target_order_id = target_order_id;
    mut_overfill.forged_qty = Qty(99999);

    auto mut_pref_alloc = make_mut(MutationType::ManipulateAllocation);
    mut_pref_alloc.target_order_id = target_order_id;
    mut_pref_alloc.forged_qty = Qty(1);

    auto mut_limit = make_mut(MutationType::ViolateLimitPrice);
    mut_limit.target_order_id = target_order_id;
    mut_limit.bogus_price = Price(99999);

    auto mut_cash = make_mut(MutationType::InflatePostStateCash);
    mut_cash.beneficiary_account = target_account_id;
    mut_cash.extra_cash = Money::from_raw(10000);

    auto mut_inv = make_mut(MutationType::InflatePostStateInventory);
    mut_inv.beneficiary_account = target_account_id;
    mut_inv.extra_instrument = inst;
    mut_inv.extra_qty = Qty(50);

    auto mut_leaf = make_mut(MutationType::SpoofCommitmentLeaf);
    mut_leaf.leaf_index = 0;
    mut_leaf.fake_commitment = Commitment::from_bytes(std::array<uint8_t, 32>{0xEE});

    auto mut_stale = make_mut(MutationType::StaleOracleSnapshot);
    mut_stale.staleness_offset_nanos = 1ULL;

    std::vector<TestCase> test_cases = {
        {
            "honest_baseline",
            "Honest operator execution following published deterministic rules",
            make_mut(MutationType::None),
            true
        },
        {
            "omitted_order_censorship",
            "Operator silently drops eligible Order 0 from batch clearing",
            mut_censor,
            false
        },
        {
            "clearing_price_inflation",
            "Operator artificially shifts clearing price by +5 ticks",
            mut_price_inf,
            false
        },
        {
            "clearing_price_deflation",
            "Operator artificially shifts clearing price by -5 ticks",
            mut_price_def,
            false
        },
        {
            "overfill_attack",
            "Operator allocates 99,999 lots to target order",
            mut_overfill,
            false
        },
        {
            "preferential_allocation",
            "Operator manipulates allocation distribution to favor cartel member",
            mut_pref_alloc,
            false
        },
        {
            "limit_price_violation",
            "Operator executes order at non-compliant price",
            mut_limit,
            false
        },
        {
            "frozen_post_state_attack",
            "Operator publishes real fills but reports a post-state in which no balance moved",
            make_mut(MutationType::FreezePostState),
            false
        },
        {
            "cash_creation_attack",
            "Operator mints $10,000 cash out of thin air",
            mut_cash,
            false
        },
        {
            "inventory_creation_attack",
            "Operator mints 50 asset lots out of thin air",
            mut_inv,
            false
        },
        {
            "commitment_leaf_spoofing",
            "Operator replaces Merkle leaf with spoofed digest",
            mut_leaf,
            false
        },
        {
            "stale_oracle_reference",
            "Operator clears batch with stale reference snapshot",
            mut_stale,
            false
        }
    };

    bool all_invariants_held = true;
    size_t total_rejected = 0;
    std::vector<AttackEvaluationResult> results;

    for (const auto& tc : test_cases) {
        AdversarialOperator op(tc.mutation);
        auto exec = op.execute(pre_state, batch, preimages);

        // Audit 1: Transition Invariant Verification
        auto trans_res = verify_transition(pre_state, exec.batch, exec.execution_result);

        // Audit 2: Complete-Input Accounting Verification against original tree
        auto acct_res = verify_complete_input_accounting(
            batch,
            original_merkle_tree,
            exec.accounting,
            exec.execution_result.allocation
        );

        // Audit 3: Oracle Validation
        Result<Ok> oracle_res = ok;
        if (exec.oracle_policy.has_value() && exec.oracle_snapshot.has_value()) {
            oracle_res = validate_oracle_reference(
                exec.oracle_snapshot.value(),
                exec.oracle_policy.value(),
                1700000000000000000ULL,
                exec.execution_result.clearing_outcome.clearing_price
            );
        }

        bool passed = trans_res.is_ok() && acct_res.is_ok() && oracle_res.is_ok();
        std::optional<std::string> rejection_reason = std::nullopt;

        if (trans_res.is_err()) {
            rejection_reason = "InvariantViolation: " + trans_res.error_message();
        } else if (acct_res.is_err()) {
            rejection_reason = "AccountingError: " + acct_res.error_message();
        } else if (oracle_res.is_err()) {
            rejection_reason = "OracleValidationError: " + oracle_res.error_message();
        }

        bool expected_met = (passed == tc.expect_pass);
        if (!expected_met) {
            all_invariants_held = false;
        }

        if (!passed) {
            total_rejected++;
        }

        results.push_back(AttackEvaluationResult{
            tc.name,
            tc.desc,
            tc.expect_pass,
            passed,
            rejection_reason,
            expected_met
        });
    }

    return AttackMatrixReport{
        batch.batch_id().as_raw(),
        batch.instrument().as_raw(),
        results.size(),
        total_rejected,
        all_invariants_held,
        std::move(results)
    };
}

} // namespace faircross
