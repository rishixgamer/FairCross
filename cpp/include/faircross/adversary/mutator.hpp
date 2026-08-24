#pragma once

#include <array>
#include <vector>
#include <optional>
#include "faircross/domain/ledger.hpp"
#include "faircross/domain/oracle.hpp"
#include "faircross/commitments/merkle.hpp"
#include "faircross/commitments/batch_commitment.hpp"
#include "faircross/engine/batch.hpp"
#include "faircross/engine/transition.hpp"
#include "faircross/engine/accounting.hpp"

namespace faircross {

enum class MutationType {
    None,
    ManipulateClearingPrice,
    CensorOrder,
    InjectUnauthorizedOrder,
    ManipulateAllocation,
    ViolateLimitPrice,
    /// Publishes real fills but reports a post-state in which no balance moved,
    /// pocketing the settlement. Aggregate cash and inventory conservation both
    /// hold trivially here, so only per-account reconstruction detects it.
    FreezePostState,
    InflatePostStateCash,
    InflatePostStateInventory,
    SpoofCommitmentLeaf,
    ReplayBatchHeader,
    StaleOracleSnapshot,
};

struct AdversarialMutation {
    MutationType type = MutationType::None;
    int64_t offset_ticks = 0;
    size_t leaf_index = 0;
    Order injected_order;
    std::array<uint8_t, 32> injected_salt{};
    OrderId target_order_id;
    Qty forged_qty;
    Price bogus_price;
    AccountId beneficiary_account;
    Money extra_cash;
    InstrumentId extra_instrument;
    Qty extra_qty;
    Commitment fake_commitment;
    BatchId fake_batch_id;
    uint64_t staleness_offset_nanos = 0;
};

struct AdversarialExecution {
    Batch batch;
    BatchExecutionResult execution_result;
    MerkleTree merkle_tree;
    CompleteInputAccounting accounting;
    BatchHeader batch_header;
    Commitment batch_commitment;
    std::optional<ReferencePriceSnapshot> oracle_snapshot;
    std::optional<ReferencePricePolicy> oracle_policy;
};

class AdversarialOperator {
public:
    AdversarialOperator() = default;
    explicit AdversarialOperator(AdversarialMutation mutation) : mutation_(mutation) {}

    static AdversarialOperator honest() { return AdversarialOperator{}; }
    static AdversarialOperator with_mutation(AdversarialMutation mutation) { return AdversarialOperator(mutation); }

    AdversarialExecution execute(
        const Ledger& pre_state,
        const Batch& batch,
        const std::vector<SaltedOrderPreimage>& preimages
    ) const;

private:
    AdversarialMutation mutation_;
};

} // namespace faircross
