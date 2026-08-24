#include "test_framework.hpp"
#include "faircross/simulator/rng.hpp"
#include "faircross/simulator/population.hpp"
#include "faircross/simulator/interval.hpp"
#include "faircross/simulator/metrics.hpp"

using namespace faircross;

TEST_CASE(test_simulator_rng_determinism) {
    DeterministicRng rng1(12345);
    DeterministicRng rng2(12345);

    for (int i = 0; i < 100; ++i) {
        REQUIRE_EQ(rng1.next_u64(), rng2.next_u64());
    }
}

TEST_CASE(test_simulator_population_session_generation) {
    PopulationSimulationConfig config{
        42,
        InstrumentId(1),
        Price(100),
        {SyntheticMarketMaker(AccountId(1), 2, Qty(10))},
        {SyntheticNoiseTrader(AccountId(2), 500, 5, Qty(1), Qty(5))},
        Money::from_raw(100000),
        Qty(1000),
        3,
        5
    };

    auto session = AgentPopulationSimulator::simulate_session(config);
    REQUIRE_EQ(session.batches.size(), 3);
    REQUIRE_EQ(session.batches[0].batch.batch_id().as_raw(), 1);
    REQUIRE(!session.batches[0].batch.is_empty());
}

TEST_CASE(test_interval_equal_arrivals_have_canonical_order) {
    const InstrumentId instrument(7);
    auto submission = [instrument](uint64_t order_id, uint8_t salt_byte) {
        std::array<uint8_t, 32> salt{};
        salt.fill(salt_byte);
        return TimestampedOrderSubmission{
            Order{OrderId(order_id), AccountId(order_id + 10), instrument,
                  Side::Buy, Price(100), Qty(1), 99},
            500,
            salt,
        };
    };

    std::vector<TimestampedOrderSubmission> forward{
        submission(1, 1), submission(2, 2), submission(3, 3)};
    std::vector<TimestampedOrderSubmission> reversed{
        submission(3, 3), submission(2, 2), submission(1, 1)};

    const auto first = BatchIntervalPartitionEngine::partition_stream(
        OrderInputStream(instrument, forward), 0, 1'000);
    const auto second = BatchIntervalPartitionEngine::partition_stream(
        OrderInputStream(instrument, reversed), 0, 1'000);

    REQUIRE_EQ(first.size(), 1);
    REQUIRE_EQ(second.size(), 1);
    REQUIRE_EQ(first[0].batch.orders(), second[0].batch.orders());
    REQUIRE_EQ(first[0].preimages, second[0].preimages);
    REQUIRE_EQ(first[0].batch.orders()[0].id, OrderId(1));
    REQUIRE_EQ(first[0].batch.orders()[1].id, OrderId(2));
    REQUIRE_EQ(first[0].batch.orders()[2].id, OrderId(3));
    REQUIRE_EQ(first[0].batch.orders()[0].seq, 0);
    REQUIRE_EQ(first[0].batch.orders()[1].seq, 1);
    REQUIRE_EQ(first[0].batch.orders()[2].seq, 2);
}
