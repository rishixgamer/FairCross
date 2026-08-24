#pragma once

#include <vector>
#include "faircross/domain/order.hpp"
#include "faircross/engine/batch.hpp"
#include "faircross/domain/ledger.hpp"
#include "faircross/simulator/config.hpp"

namespace faircross {

/// Synthetic batch container bundled with salted preimages and cutoff metadata.
struct SyntheticBatchBundle {
    Batch batch;
    std::vector<SaltedOrderPreimage> preimages;
    uint64_t cutoff_nanos;

    auto operator<=>(const SyntheticBatchBundle&) const = default;
};

/// A complete multi-batch synthetic simulation session.
struct SyntheticSession {
    SyntheticFlowConfig config;
    Ledger genesis_ledger;
    std::vector<SyntheticBatchBundle> batches;
};

/// Pure deterministic synthetic order-flow generator.
///
/// The sequence of RNG draws is part of the contract, not an implementation
/// detail: the session is derived from a seeded SplitMix64, so reordering or
/// adding a draw silently produces a different session from the same seed and
/// invalidates every recorded benchmark that used it.
class SyntheticOrderFlowSimulator {
public:
    static SyntheticSession generate_session(const SyntheticFlowConfig& config);
};

} // namespace faircross
