// Property-based tests for the FairCross market invariants.
//
// Covers permutation invariance, deterministic clearing and tie-breaking, and
// the conservation properties that the project's minimum invariant set requires
// but that example tests cover poorly.
//
// The harness in property.hpp generates deterministically and prints a
// reproducing seed on failure; see that file for the environment variables.

#include "test_framework.hpp"
#include "property.hpp"

#include "faircross/domain/ledger.hpp"
#include "faircross/engine/batch.hpp"
#include "faircross/engine/transition.hpp"
#include "faircross/engine/checker.hpp"
#include "faircross/engine/accounting.hpp"
#include "faircross/engine/auction.hpp"
#include "faircross/engine/allocation.hpp"

#include <map>
#include <string>
#include <vector>

using namespace faircross;
namespace pt = faircross::property;

namespace {

constexpr size_t kMaxOrders = 20;

/// Funds every account referenced by the batch well beyond what it could spend,
/// so a rejection reflects a market-semantics violation rather than an
/// incidental shortfall.
Ledger funded_ledger_for(const std::vector<Order>& orders, InstrumentId inst) {
    Ledger ledger;
    for (const Order& o : orders) {
        AccountState& acc = ledger.get_or_create_account(o.account);
        if (acc.cash().is_zero()) {
            const auto res = acc.credit_cash(Money::from_raw(100'000'000));
            if (res.is_err()) return ledger;
            const auto inv = acc.credit_inventory(inst, Qty(100'000));
            if (inv.is_err()) return ledger;
        }
    }
    return ledger;
}

/// Fisher-Yates driven by an LCG, so a permutation is reproducible from a seed.
std::vector<Order> permute(const std::vector<Order>& orders, uint64_t perm_seed) {
    std::vector<Order> out = orders;
    uint64_t state = perm_seed;
    for (size_t i = out.size(); i-- > 1;) {
        state = state * 6364136223846793005ULL + 1ULL;
        const size_t j = static_cast<size_t>(state) % (i + 1);
        std::swap(out[i], out[j]);
    }
    return out;
}

Money total_cash(const Ledger& ledger) {
    auto res = ledger.total_cash();
    return res.is_ok() ? res.value() : Money::zero();
}

Qty total_inventory(const Ledger& ledger, InstrumentId inst) {
    auto res = ledger.total_inventory_of(inst);
    return res.is_ok() ? res.value() : Qty::zero();
}

} // namespace

TEST_CASE(prop_batch_permutation_invariance) {
    // Intake order must not be observable in any published output. The batch
    // canonicalizes on (seq, order_id), so an arbitrary submission order has to
    // produce a bit-identical clearing outcome, allocation, fills, and ledger.
    pt::for_all_batches(
        "batch_permutation_invariance", 1000, kMaxOrders,
        [](const std::vector<Order>& orders) -> std::string {
            const InstrumentId inst(1);
            const Ledger pre_state = funded_ledger_for(orders, inst);

            Batch batch1(BatchId(1), inst, orders);
            auto res1 = execute_batch(pre_state, batch1);
            if (res1.is_err()) return "canonical batch failed to execute: " + res1.error_message();

            // Permutation seed derived from the batch itself, so the case index
            // alone reproduces both the orders and their shuffle.
            uint64_t perm_seed = 0x243F6A8885A308D3ULL;
            for (const Order& o : orders) {
                perm_seed ^= o.price.as_raw() * 31 + o.qty.as_raw();
                perm_seed = perm_seed * 6364136223846793005ULL + 1ULL;
            }

            Batch batch2(BatchId(1), inst, permute(orders, perm_seed));
            auto res2 = execute_batch(pre_state, batch2);
            if (res2.is_err()) return "permuted batch failed to execute: " + res2.error_message();

            const BatchExecutionResult& a = res1.value();
            const BatchExecutionResult& b = res2.value();

            if (!(a.clearing_outcome == b.clearing_outcome)) return "clearing outcome differs";
            if (!(a.allocation == b.allocation)) return "allocation differs";
            if (!(a.fills == b.fills)) return "canonical fills differ";
            if (!(a.post_state == b.post_state)) return "post-state ledger differs";

            if (verify_transition(pre_state, batch1, a).is_err()) {
                return "canonical transition rejected by checker";
            }
            if (verify_transition(pre_state, batch2, b).is_err()) {
                return "permuted transition rejected by checker";
            }
            return {};
        });
}

TEST_CASE(prop_deterministic_clearing_and_tie_breaking) {
    // Where ordering does matter -- tie-breaking among equally optimal prices,
    // and largest-remainder pro-rata among equal at-the-money orders -- the
    // choice must be a pure function of the batch, not of evaluation order.
    pt::for_all_batches(
        "deterministic_clearing_and_tie_breaking", 1000, kMaxOrders,
        [](const std::vector<Order>& orders) -> std::string {
            const InstrumentId inst(1);
            Batch batch(BatchId(1), inst, orders);

            auto first = determine_clearing_outcome(batch);
            if (first.is_err()) return "clearing failed: " + first.error_message();

            // Repeated evaluation must agree with itself.
            for (int repeat = 0; repeat < 3; ++repeat) {
                auto again = determine_clearing_outcome(batch);
                if (again.is_err()) return "clearing became an error on repeat";
                if (!(again.value() == first.value())) return "clearing outcome is not deterministic";
            }

            // Candidate prices are deduplicated and strictly ascending, which is
            // what makes the tie-break rule well defined.
            const std::vector<Price> candidates = candidate_prices_from_orders(orders);
            for (size_t i = 1; i < candidates.size(); ++i) {
                if (!(candidates[i - 1].as_raw() < candidates[i].as_raw())) {
                    return "candidate prices are not strictly ascending at index " +
                           std::to_string(i);
                }
            }

            const ClearingOutcome& outcome = first.value();
            if (!outcome.clearing_price.has_value()) {
                if (!outcome.executable_volume.is_zero()) {
                    return "no clearing price but non-zero executable volume";
                }
                return {};
            }

            // Allocation must likewise be a pure function, including the
            // largest-remainder tie-break by ascending sequence.
            auto alloc_a = allocate_batch(batch, outcome.clearing_price, outcome.executable_volume);
            auto alloc_b = allocate_batch(batch, outcome.clearing_price, outcome.executable_volume);
            if (alloc_a.is_err() || alloc_b.is_err()) return "allocation failed";
            if (!(alloc_a.value() == alloc_b.value())) return "allocation is not deterministic";

            // Allocations are reported in canonical batch order.
            const auto& allocations = alloc_a.value().allocations;
            for (size_t i = 1; i < allocations.size(); ++i) {
                if (allocations[i - 1].seq > allocations[i].seq) {
                    return "allocations are not in ascending sequence order";
                }
            }
            return {};
        });
}

TEST_CASE(prop_no_balance_or_asset_creation) {
    // The two conservation invariants: a batch may move cash and inventory
    // between accounts but must never mint or destroy either.
    pt::for_all_batches(
        "no_balance_or_asset_creation", 1000, kMaxOrders,
        [](const std::vector<Order>& orders) -> std::string {
            const InstrumentId inst(1);
            const Ledger pre_state = funded_ledger_for(orders, inst);

            Batch batch(BatchId(1), inst, orders);
            auto exec_res = execute_batch(pre_state, batch);
            if (exec_res.is_err()) return "execution failed: " + exec_res.error_message();
            const BatchExecutionResult& exec = exec_res.value();

            if (!(total_cash(pre_state) == total_cash(exec.post_state))) {
                return "total cash changed across the batch";
            }
            if (!(total_inventory(pre_state, inst) == total_inventory(exec.post_state, inst))) {
                return "total inventory changed across the batch";
            }

            // No account may end with more inventory than the whole system held,
            // and every account must still exist.
            for (const auto& [acc_id, post_acc] : exec.post_state.accounts()) {
                const AccountState* pre_acc = pre_state.get_account(acc_id);
                if (pre_acc == nullptr) return "post-state introduced an account absent pre-trade";
                if (post_acc.inventory_of(inst).as_raw() >
                    total_inventory(pre_state, inst).as_raw()) {
                    return "an account holds more inventory than the system total";
                }
            }

            // Fills must not exceed the submitted quantity of their order.
            std::map<uint64_t, uint64_t> submitted;
            for (const Order& o : orders) submitted[o.id.as_raw()] = o.qty.as_raw();
            std::map<uint64_t, uint64_t> filled;
            for (const Fill& f : exec.fills) filled[f.order_id.as_raw()] += f.fill_qty.as_raw();
            for (const auto& [order_id, qty] : filled) {
                if (qty > submitted[order_id]) {
                    return "order " + std::to_string(order_id) + " was overfilled";
                }
            }

            if (verify_transition(pre_state, batch, exec).is_err()) {
                return "checker rejected an honestly executed transition";
            }
            return {};
        });
}

TEST_CASE(prop_complete_input_accounting_always_valid) {
    // Every committed order is accounted for exactly once, included in the
    // Merkle root, and carries the disposition its allocation implies.
    pt::for_all_batches(
        "complete_input_accounting_always_valid", 500, kMaxOrders,
        [](const std::vector<Order>& orders) -> std::string {
            const InstrumentId inst(1);
            Batch batch(BatchId(1), inst, orders);

            std::vector<SaltedOrderPreimage> preimages;
            preimages.reserve(orders.size());
            for (size_t i = 0; i < orders.size(); ++i) {
                std::array<uint8_t, 32> nonce{};
                nonce.fill(static_cast<uint8_t>(i));
                preimages.emplace_back(orders[i], nonce);
            }

            auto outcome = determine_clearing_outcome(batch);
            if (outcome.is_err()) return "clearing failed: " + outcome.error_message();

            auto alloc = allocate_batch(batch, outcome.value().clearing_price,
                                        outcome.value().executable_volume);
            if (alloc.is_err()) return "allocation failed: " + alloc.error_message();

            auto [tree, manifest] =
                build_complete_input_accounting(batch, preimages, alloc.value());

            if (manifest.total_committed != batch.len()) {
                return "total_committed " + std::to_string(manifest.total_committed) +
                       " != batch length " + std::to_string(batch.len());
            }

            auto verdict =
                verify_complete_input_accounting(batch, tree, manifest, alloc.value());
            if (verdict.is_err()) {
                return "accounting rejected: " + verdict.error_message();
            }
            return {};
        });
}

TEST_CASE(prop_checker_accepts_only_the_canonical_post_state) {
    // Aggregate conservation alone admits forgeries in which the
    // reported fills settled but no balance moved. The checker must now accept
    // exactly the post-state execute_batch produces, and nothing else.
    pt::for_all_batches(
        "checker_accepts_only_the_canonical_post_state", 1000, kMaxOrders,
        [](const std::vector<Order>& orders) -> std::string {
            const InstrumentId inst(1);
            const Ledger pre_state = funded_ledger_for(orders, inst);

            Batch batch(BatchId(1), inst, orders);
            auto exec_res = execute_batch(pre_state, batch);
            if (exec_res.is_err()) return "execution failed: " + exec_res.error_message();
            const BatchExecutionResult& exec = exec_res.value();

            if (verify_transition(pre_state, batch, exec).is_err()) {
                return "honest post-state was rejected";
            }
            if (exec.fills.empty()) return {};

            // Claiming nothing settled must be rejected. Guarded on the batch
            // actually moving a balance: a self-trade -- one account both buying
            // and selling the same quantity at the clearing price -- nets to
            // zero, so an unchanged ledger is the correct post-state there --
            // a case property testing surfaced and example tests had missed.
            if (!(exec.post_state == pre_state)) {
                BatchExecutionResult frozen = exec;
                frozen.post_state = pre_state;
                if (verify_transition(pre_state, batch, frozen).is_ok()) {
                    return "a post-state where no balance moved was accepted";
                }
            }

            // Moving one unit of cash between accounts leaves the system total
            // unchanged but must still be rejected.
            std::vector<AccountId> ids;
            for (const auto& [id, _] : exec.post_state.accounts()) ids.push_back(id);
            if (ids.size() >= 2) {
                const AccountState* giver = exec.post_state.get_account(ids[1]);
                if (giver != nullptr && !giver->cash().is_zero()) {
                    BatchExecutionResult skimmed = exec;
                    if (skimmed.post_state.get_or_create_account(ids[1])
                            .debit_cash(Money::from_raw(1)).is_ok() &&
                        skimmed.post_state.get_or_create_account(ids[0])
                            .credit_cash(Money::from_raw(1)).is_ok()) {
                        if (verify_transition(pre_state, batch, skimmed).is_ok()) {
                            return "cash moved between accounts was accepted";
                        }
                    }
                }
            }

            // An account absent pre-trade must not appear in an accepted post-state.
            BatchExecutionResult padded = exec;
            padded.post_state.insert_account(
                AccountState(AccountId(9'999'999), Money::from_raw(0)));
            if (verify_transition(pre_state, batch, padded).is_ok()) {
                return "an account absent pre-trade was accepted in the post-state";
            }
            return {};
        });
}
