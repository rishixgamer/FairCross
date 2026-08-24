#include "faircross/simulator/population.hpp"
#include <algorithm>

namespace faircross {

PopulationSession AgentPopulationSimulator::simulate_session(const PopulationSimulationConfig& config) {
    DeterministicRng rng(config.seed);

    // 1. Build genesis ledger
    Ledger genesis_ledger;
    std::vector<AccountId> all_accounts;
    for (const auto& mm : config.market_makers) all_accounts.push_back(mm.account_id);
    for (const auto& nt : config.noise_traders) all_accounts.push_back(nt.account_id);

    std::sort(all_accounts.begin(), all_accounts.end(), [](AccountId a, AccountId b) {
        return a.as_raw() < b.as_raw();
    });
    all_accounts.erase(std::unique(all_accounts.begin(), all_accounts.end()), all_accounts.end());

    for (AccountId acc_id : all_accounts) {
        AccountState acc(acc_id, config.initial_cash);
        auto _ = acc.credit_inventory(config.instrument_id, config.initial_inventory);
        genesis_ledger.insert_account(std::move(acc));
    }

    // 2. Generate batches
    MarketEnvironment env{
        config.instrument_id,
        config.initial_mid_price,
        std::nullopt,
        1
    };

    std::vector<SyntheticBatchBundle> batches;
    batches.reserve(config.num_batches);
    uint64_t start_time_nanos = 1700000000000000000ULL;

    for (size_t batch_idx = 1; batch_idx <= config.num_batches; ++batch_idx) {
        BatchId batch_id(batch_idx);
        uint64_t cutoff_nanos = start_time_nanos + static_cast<uint64_t>(batch_idx) * 100000000ULL;

        std::vector<Order> batch_orders;

        for (const auto& mm : config.market_makers) {
            auto mm_orders = mm.generate_orders(env, rng);
            batch_orders.insert(batch_orders.end(), mm_orders.begin(), mm_orders.end());
        }

        if (!config.noise_traders.empty()) {
            for (size_t i = 0; i < config.taker_orders_per_batch; ++i) {
                size_t nt_idx = static_cast<size_t>(rng.gen_range_u64(0, config.noise_traders.size() - 1));
                const auto& nt = config.noise_traders[nt_idx];
                batch_orders.push_back(nt.generate_order(env, rng));
            }
        }

        std::sort(batch_orders.begin(), batch_orders.end(), [](const Order& a, const Order& b) {
            return a.id.as_raw() < b.id.as_raw();
        });

        for (size_t seq = 0; seq < batch_orders.size(); ++seq) {
            batch_orders[seq].seq = seq;
        }

        std::vector<SaltedOrderPreimage> preimages;
        preimages.reserve(batch_orders.size());
        for (const auto& o : batch_orders) {
            std::array<uint8_t, 32> salt{};
            for (size_t j = 0; j < 4; ++j) {
                uint64_t r = rng.next_u64();
                for (size_t b = 0; b < 8; ++b) {
                    salt[j * 8 + b] = static_cast<uint8_t>((r >> (b * 8)) & 0xFF);
                }
            }
            preimages.push_back(SaltedOrderPreimage(o, salt));
        }

        Batch batch(batch_id, config.instrument_id, batch_orders);
        batches.push_back(SyntheticBatchBundle{
            std::move(batch),
            std::move(preimages),
            cutoff_nanos
        });
    }

    return PopulationSession{
        config,
        std::move(genesis_ledger),
        std::move(batches)
    };
}

} // namespace faircross
