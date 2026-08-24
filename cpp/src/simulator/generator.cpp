#include "faircross/simulator/generator.hpp"
#include "faircross/simulator/rng.hpp"

#include <algorithm>

namespace faircross {

SyntheticSession SyntheticOrderFlowSimulator::generate_session(
    const SyntheticFlowConfig& config
) {
    DeterministicRng rng(config.seed);

    // 1. Genesis ledger.
    Ledger genesis_ledger;
    const size_t num_accs = std::max<size_t>(config.num_accounts, 2);
    for (size_t acc_idx = 1; acc_idx <= num_accs; ++acc_idx) {
        AccountState acc(AccountId(static_cast<uint64_t>(acc_idx)),
                         config.initial_cash_per_account);
        const auto credited =
            acc.credit_inventory(config.instrument_id, config.initial_inventory_per_account);
        (void)credited;
        genesis_ledger.insert_account(std::move(acc));
    }

    // 2. Sequential batches.
    std::vector<SyntheticBatchBundle> batches;
    batches.reserve(config.num_batches);
    uint64_t global_order_id = 1;
    constexpr uint64_t kStartTimeNanos = 1'700'000'000'000'000'000ULL;

    for (size_t batch_idx = 1; batch_idx <= config.num_batches; ++batch_idx) {
        const uint64_t cutoff_nanos =
            kStartTimeNanos + static_cast<uint64_t>(batch_idx) * 100'000'000ULL;

        const auto order_count = static_cast<size_t>(rng.gen_range_u64(
            static_cast<uint64_t>(config.min_orders_per_batch),
            static_cast<uint64_t>(config.max_orders_per_batch)));

        std::vector<Order> orders;
        std::vector<SaltedOrderPreimage> preimages;
        orders.reserve(order_count);
        preimages.reserve(order_count);

        for (size_t seq = 0; seq < order_count; ++seq) {
            const OrderId order_id(global_order_id++);
            const AccountId acc_id(rng.gen_range_u64(1, static_cast<uint64_t>(num_accs)));

            const bool is_buy = rng.gen_bool_permille(config.buy_probability_permille);
            const Side side = is_buy ? Side::Buy : Side::Sell;

            const uint64_t mid_raw = config.price_mid.as_raw();
            const uint64_t delta = rng.gen_range_u64(0, config.price_spread_ticks);

            // Saturating arithmetic: a spread wider than the mid must clamp at
            // zero rather than wrap into an enormous price.
            uint64_t price_raw;
            if (is_buy) {
                const uint64_t added = mid_raw + delta / 2;
                const uint64_t sub = delta % 3;
                price_raw = added > sub ? added - sub : 0;
            } else {
                const uint64_t half = delta / 2;
                const uint64_t subbed = mid_raw > half ? mid_raw - half : 0;
                price_raw = subbed + delta % 3;
            }
            price_raw = std::max<uint64_t>(price_raw, 1);

            const uint64_t qty_raw =
                rng.gen_range_u64(config.min_order_qty.as_raw(), config.max_order_qty.as_raw());

            const Order order{
                order_id,
                acc_id,
                config.instrument_id,
                side,
                Price(price_raw),
                Qty(qty_raw),
                static_cast<uint64_t>(seq),
            };

            // Four 64-bit draws, little-endian. The draw order is part of the
            // seed contract; changing it changes every seeded session.
            std::array<uint8_t, 32> salt{};
            for (size_t word = 0; word < 4; ++word) {
                const uint64_t r = rng.next_u64();
                for (size_t b = 0; b < 8; ++b) {
                    salt[word * 8 + b] = static_cast<uint8_t>((r >> (b * 8)) & 0xFF);
                }
            }

            orders.push_back(order);
            preimages.emplace_back(order, salt);
        }

        Batch batch(BatchId(static_cast<uint64_t>(batch_idx)), config.instrument_id, orders);
        batches.push_back(SyntheticBatchBundle{std::move(batch), std::move(preimages),
                                               cutoff_nanos});
    }

    return SyntheticSession{config, std::move(genesis_ledger), std::move(batches)};
}

} // namespace faircross
