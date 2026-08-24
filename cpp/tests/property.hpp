#pragma once

// Minimal dependency-free property-testing harness for the C++ test suite.
//
// FairCross keeps third-party dependencies minimal, so property testing gets a
// small in-tree harness rather than a vendored library. It provides what the
// project's testing rules actually require: deterministic generation, greedy
// shrinking to a small counterexample, and a printed seed so any failure is
// reproducible.
//
// Reproducing a failure:
//   FAIRCROSS_PROPERTY_SEED=<seed> ./bin/faircross_tests
// Widening a run:
//   FAIRCROSS_PROPERTY_CASES=10000 ./bin/faircross_tests

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "test_framework.hpp"
#include "faircross/domain/order.hpp"
#include "faircross/domain/primitives.hpp"

namespace faircross::property {

/// SplitMix64, matching `DeterministicRng` in the simulator. Deliberately a
/// separate copy: a test harness that shares state with the code under test
/// cannot be trusted to reproduce a failure after that code changes.
class Rng {
public:
    explicit constexpr Rng(uint64_t seed) noexcept : state_(seed) {}

    uint64_t next_u64() noexcept {
        state_ += 0x9E3779B97F4A7C15ULL;
        uint64_t z = state_;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    uint64_t in_range(uint64_t min, uint64_t max) noexcept {
        if (min >= max) return min;
        return min + (next_u64() % (max - min + 1));
    }

    bool coin() noexcept { return (next_u64() & 1u) != 0; }

private:
    uint64_t state_;
};

/// Generation bounds for synthesized batches. Order ids and sequence numbers
/// are assigned positionally, which is what `Batch` validation requires.
inline constexpr uint64_t kMaxAccounts = 10;
inline constexpr uint64_t kMinPrice = 1;
inline constexpr uint64_t kMaxPrice = 5000;
inline constexpr uint64_t kMinQty = 1;
inline constexpr uint64_t kMaxQty = 500;

inline std::vector<Order> generate_batch_orders(Rng& rng, InstrumentId instrument,
                                                size_t max_orders) {
    const auto count = static_cast<size_t>(rng.in_range(1, max_orders));
    std::vector<Order> orders;
    orders.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        orders.push_back(Order{
            OrderId(static_cast<uint64_t>(i) + 1),
            AccountId(rng.in_range(1, kMaxAccounts)),
            instrument,
            rng.coin() ? Side::Buy : Side::Sell,
            Price(rng.in_range(kMinPrice, kMaxPrice)),
            Qty(rng.in_range(kMinQty, kMaxQty)),
            static_cast<uint64_t>(i),
        });
    }
    return orders;
}

/// Reassigns positional ids and sequence numbers after a shrink step removes an
/// order, so the candidate stays a valid batch.
inline void renumber(std::vector<Order>& orders) {
    for (size_t i = 0; i < orders.size(); ++i) {
        orders[i].id = OrderId(static_cast<uint64_t>(i) + 1);
        orders[i].seq = static_cast<uint64_t>(i);
    }
}

/// Greedy shrinker: drop orders while the property still fails, then reduce
/// individual price and quantity magnitudes. Reports the smallest failing case
/// rather than the raw random one, which is usually far easier to read.
inline std::vector<Order> shrink(const std::vector<Order>& failing,
                                 const std::function<bool(const std::vector<Order>&)>& still_fails) {
    std::vector<Order> best = failing;

    // Pass 1: remove orders one at a time.
    bool progress = true;
    while (progress && best.size() > 1) {
        progress = false;
        for (size_t i = 0; i < best.size(); ++i) {
            std::vector<Order> candidate = best;
            candidate.erase(candidate.begin() + static_cast<long>(i));
            renumber(candidate);
            if (!candidate.empty() && still_fails(candidate)) {
                best = std::move(candidate);
                progress = true;
                break;
            }
        }
    }

    // Pass 2: pull price and quantity toward their minimums.
    for (size_t i = 0; i < best.size(); ++i) {
        // Price
        while (best[i].price.as_raw() > kMinPrice) {
            std::vector<Order> candidate = best;
            candidate[i].price = Price(candidate[i].price.as_raw() / 2 > kMinPrice
                                           ? candidate[i].price.as_raw() / 2
                                           : kMinPrice);
            if (!still_fails(candidate)) break;
            best = std::move(candidate);
        }
        // Quantity
        while (best[i].qty.as_raw() > kMinQty) {
            std::vector<Order> candidate = best;
            candidate[i].qty = Qty(candidate[i].qty.as_raw() / 2 > kMinQty
                                       ? candidate[i].qty.as_raw() / 2
                                       : kMinQty);
            if (!still_fails(candidate)) break;
            best = std::move(candidate);
        }
    }

    return best;
}

inline std::string describe(const std::vector<Order>& orders) {
    std::string out = "[" + std::to_string(orders.size()) + " orders]";
    for (const Order& o : orders) {
        out += "\n    {id=" + std::to_string(o.id.as_raw()) +
               " acct=" + std::to_string(o.account.as_raw()) +
               " side=" + std::string(o.side == Side::Buy ? "buy" : "sell") +
               " px=" + std::to_string(o.price.as_raw()) +
               " qty=" + std::to_string(o.qty.as_raw()) +
               " seq=" + std::to_string(o.seq) + "}";
    }
    return out;
}

inline uint64_t base_seed() {
    if (const char* env = std::getenv("FAIRCROSS_PROPERTY_SEED")) {
        return std::strtoull(env, nullptr, 10);
    }
    // Fixed default: property tests must be reproducible run to run, so the
    // seed never comes from the clock.
    return 0x5EED'C0FFEE'1234ULL;
}

inline size_t case_count(size_t fallback) {
    if (const char* env = std::getenv("FAIRCROSS_PROPERTY_CASES")) {
        const auto parsed = std::strtoull(env, nullptr, 10);
        if (parsed > 0) return static_cast<size_t>(parsed);
    }
    return fallback;
}

/// Runs `check` over `cases` generated batches.
///
/// `check` returns an empty string on success, or a description of the
/// violated invariant. On failure the batch is shrunk and the seed that
/// reproduces the whole run is printed before the test fails.
inline void for_all_batches(const std::string& property_name,
                            size_t cases,
                            size_t max_orders,
                            const std::function<std::string(const std::vector<Order>&)>& check) {
    const uint64_t seed = base_seed();
    const size_t total = case_count(cases);

    auto fails = [&check](const std::vector<Order>& orders) {
        return !check(orders).empty();
    };

    for (size_t i = 0; i < total; ++i) {
        // Derive a per-case seed so a single case can be replayed without
        // stepping through the ones before it.
        Rng rng(seed + i * 0x9E3779B97F4A7C15ULL);
        std::vector<Order> orders = generate_batch_orders(rng, InstrumentId(1), max_orders);

        const std::string failure = check(orders);
        if (failure.empty()) continue;

        const std::vector<Order> minimal = shrink(orders, fails);
        const std::string minimal_failure = check(minimal);

        std::cerr << "\n  PROPERTY FAILED: " << property_name << "\n"
                  << "    violated: " << (minimal_failure.empty() ? failure : minimal_failure) << "\n"
                  << "    case index: " << i << " of " << total << "\n"
                  << "    reproduce:  FAIRCROSS_PROPERTY_SEED=" << seed
                  << " ./bin/faircross_tests\n"
                  << "    shrunk counterexample: " << describe(minimal) << "\n";

        throw faircross::test::TestFailure(property_name + ": " +
                                           (minimal_failure.empty() ? failure : minimal_failure));
    }
}

} // namespace faircross::property
