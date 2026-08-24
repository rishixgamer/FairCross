#include "faircross/adversary/mutator.hpp"

namespace faircross {

AdversarialExecution AdversarialOperator::execute(
    const Ledger& pre_state,
    const Batch& batch,
    const std::vector<SaltedOrderPreimage>& preimages
) const {
    std::vector<SaltedOrderPreimage> working_preimages = preimages;

    // 1. Pre-intake mutations
    if (mutation_.type == MutationType::CensorOrder) {
        if (mutation_.leaf_index < working_preimages.size()) {
            working_preimages.erase(working_preimages.begin() + static_cast<std::ptrdiff_t>(mutation_.leaf_index));
        }
    } else if (mutation_.type == MutationType::InjectUnauthorizedOrder) {
        working_preimages.push_back(SaltedOrderPreimage(mutation_.injected_order, mutation_.injected_salt));
    }

    std::vector<Order> working_orders;
    working_orders.reserve(working_preimages.size());
    for (const auto& p : working_preimages) {
        working_orders.push_back(p.order);
    }

    Batch working_batch(batch.batch_id(), batch.instrument(), working_orders);

    // 2. Clearing computation
    auto outcome = determine_clearing_outcome(working_batch).value();
    if (mutation_.type == MutationType::ManipulateClearingPrice && outcome.clearing_price.has_value()) {
        int64_t new_raw = static_cast<int64_t>(outcome.clearing_price->as_raw()) + mutation_.offset_ticks;
        if (new_raw < 1) new_raw = 1;
        outcome.clearing_price = Price(static_cast<uint64_t>(new_raw));
    }

    // 3. Allocation computation
    auto allocation = allocate_batch(working_batch, outcome.clearing_price, outcome.executable_volume).value();
    if (mutation_.type == MutationType::ManipulateAllocation) {
        for (auto& a : allocation.allocations) {
            if (a.order_id == mutation_.target_order_id) {
                a.allocated_qty = mutation_.forged_qty;
            }
        }
    }

    // 4. Fill generation
    auto fills = generate_canonical_fills(working_batch, allocation).value();
    if (mutation_.type == MutationType::ViolateLimitPrice) {
        for (auto& f : fills) {
            if (f.order_id == mutation_.target_order_id) {
                f.execution_price = mutation_.bogus_price;
                f.consideration = Money::from_price_qty(mutation_.bogus_price, f.fill_qty).value();
            }
        }
    }

    // 5. Post-state application
    Ledger post_state = pre_state;
    for (const auto& fill : fills) {
        auto& acc = post_state.get_or_create_account(fill.account_id);
        if (fill.side == Side::Buy) {
            auto _ = acc.debit_cash(fill.consideration);
            auto __ = acc.credit_inventory(fill.instrument_id, fill.fill_qty);
        } else {
            auto _ = acc.debit_inventory(fill.instrument_id, fill.fill_qty);
            auto __ = acc.credit_cash(fill.consideration);
        }
    }

    if (mutation_.type == MutationType::FreezePostState) {
        post_state = pre_state;
    } else if (mutation_.type == MutationType::InflatePostStateCash) {
        auto& acc = post_state.get_or_create_account(mutation_.beneficiary_account);
        auto _ = acc.credit_cash(mutation_.extra_cash);
    } else if (mutation_.type == MutationType::InflatePostStateInventory) {
        auto& acc = post_state.get_or_create_account(mutation_.beneficiary_account);
        auto _ = acc.credit_inventory(mutation_.extra_instrument, mutation_.extra_qty);
    }

    // 6. Complete-input accounting and Merkle tree
    auto [merkle_tree, accounting] = build_complete_input_accounting(working_batch, working_preimages, allocation);

    if (mutation_.type == MutationType::SpoofCommitmentLeaf) {
        std::vector<Commitment> leaves = merkle_tree.leaves();
        if (mutation_.leaf_index < leaves.size()) {
            leaves[mutation_.leaf_index] = mutation_.fake_commitment;
            merkle_tree = MerkleTree(leaves);
            if (mutation_.leaf_index < accounting.records.size()) {
                accounting.records[mutation_.leaf_index].commitment = mutation_.fake_commitment;
            }
        }
    }

    // 7. Batch header and commitment
    BatchId b_id = working_batch.batch_id();
    if (mutation_.type == MutationType::ReplayBatchHeader) {
        b_id = mutation_.fake_batch_id;
    }

    BatchHeader batch_header(
        1,
        b_id,
        working_batch.instrument(),
        1700000000ULL,
        static_cast<uint32_t>(working_batch.len()),
        merkle_tree.root()
    );
    Commitment batch_commitment = compute_batch_commitment(batch_header);

    // 8. Oracle reference
    uint64_t cutoff_nanos = 1700000000000000000ULL;
    uint64_t max_staleness = 1000000000ULL;
    ReferencePricePolicy policy(max_staleness, 10);
    uint64_t snapshot_time = cutoff_nanos - 500000000ULL;
    if (mutation_.type == MutationType::StaleOracleSnapshot) {
        snapshot_time = cutoff_nanos - max_staleness - mutation_.staleness_offset_nanos;
    }

    Price ref_price = outcome.clearing_price.value_or(Price(100));
    ReferencePriceSnapshot snapshot(1, working_batch.instrument(), ref_price, snapshot_time, 1);

    BatchExecutionResult exec_result{
        outcome,
        std::move(allocation),
        std::move(fills),
        std::move(post_state)
    };

    return AdversarialExecution{
        working_batch,
        std::move(exec_result),
        std::move(merkle_tree),
        std::move(accounting),
        batch_header,
        batch_commitment,
        snapshot,
        policy
    };
}

} // namespace faircross
