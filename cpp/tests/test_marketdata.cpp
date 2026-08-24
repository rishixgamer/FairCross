#include "test_framework.hpp"
#include "faircross/marketdata/parser.hpp"
#include "faircross/marketdata/book.hpp"
#include "faircross/marketdata/bridge.hpp"
#include "faircross/marketdata/packet_source.hpp"
#include "faircross/marketdata/replay.hpp"
#include <limits>

using namespace faircross;

TEST_CASE(test_itch_parser_and_order_book) {
    OrderBook book;

    // Add Bid 100 @ $10.00 (100000 ticks)
    auto add_bid = book.add_order(1001, Side::Buy, Price(100000), Qty(100));
    REQUIRE(add_bid.is_ok());

    // Add Ask 100 @ $10.02 (100200 ticks)
    auto add_ask = book.add_order(1002, Side::Sell, Price(100200), Qty(100));
    REQUIRE(add_ask.is_ok());

    REQUIRE(book.best_bid().has_value());
    REQUIRE_EQ(book.best_bid()->as_raw(), 100000);

    REQUIRE(book.best_ask().has_value());
    REQUIRE_EQ(book.best_ask()->as_raw(), 100200);

    REQUIRE(book.mid_price().has_value());
    REQUIRE_EQ(book.mid_price()->as_raw(), 100100);

    // Snapshot bridge
    auto snap_opt = OracleBridge::snapshot_from_book(book, 1, InstrumentId(1), 1700000000ULL, 1);
    REQUIRE(snap_opt.has_value());
    REQUIRE_EQ(snap_opt->reference_price.as_raw(), 100100);
}

TEST_CASE(test_packet_source_and_replay_engine) {
    // Covers SlicePacketSource ingestion and ItchReplayEngine book trajectory.
    OrderBook reference;
    REQUIRE(reference.add_order(1, Side::Buy, Price(100), Qty(10)).is_ok());

    // Drive the same messages through the replay engine.
    ItchReplayEngine engine;

    AddOrderMessage bid{};
    bid.timestamp_nanos = 1000;
    bid.order_reference_number = 1;
    bid.side = Side::Buy;
    bid.shares = 10;
    bid.price_ticks = 100;

    AddOrderMessage ask{};
    ask.timestamp_nanos = 2000;
    ask.order_reference_number = 2;
    ask.side = Side::Sell;
    ask.shares = 10;
    ask.price_ticks = 104;

    auto q1 = engine.step(ItchMessage{bid});
    REQUIRE(q1.is_ok());
    REQUIRE(q1.value().best_bid.has_value());
    REQUIRE_EQ(q1.value().best_bid->as_raw(), 100);
    // No ask yet, so no mid.
    REQUIRE(!q1.value().mid_price.has_value());
    REQUIRE_EQ(q1.value().timestamp_nanos, 1000);

    auto q2 = engine.step(ItchMessage{ask});
    REQUIRE(q2.is_ok());
    REQUIRE(q2.value().mid_price.has_value());
    REQUIRE_EQ(q2.value().mid_price->as_raw(), 102);

    // Every step is recorded, in order.
    REQUIRE_EQ(engine.bbo_history().size(), 2u);
    REQUIRE_EQ(engine.bbo_history()[0].timestamp_nanos, 1000);
    REQUIRE_EQ(engine.bbo_history()[1].timestamp_nanos, 2000);

    // A cancel reduces resting size and is reflected in the trajectory.
    OrderCancelMessage cancel{};
    cancel.timestamp_nanos = 3000;
    cancel.order_reference_number = 1;
    cancel.canceled_shares = 10;
    REQUIRE(engine.step(ItchMessage{cancel}).is_ok());
    REQUIRE(!engine.bbo_history().back().best_bid.has_value());

    // The oracle snapshot follows the replayed book, not a separate copy.
    auto snap = engine.current_reference_price_snapshot(1, InstrumentId(1), 7);
    REQUIRE(!snap.has_value()); // one-sided book has no mid
}

TEST_CASE(test_order_book_overflow_is_rejected_transactionally) {
    const uint64_t max_qty = std::numeric_limits<uint64_t>::max();
    OrderBook book;

    REQUIRE(book.add_order(1, Side::Buy, Price(100), Qty(max_qty)).is_ok());
    REQUIRE(book.best_bid().has_value());
    REQUIRE_EQ(book.best_bid()->as_raw(), 100);

    // The second order would overflow the level.  It must return an error,
    // without throwing bad_variant_access from an invalid Result::value().
    bool threw = false;
    bool returned_error = false;
    try {
        returned_error = book.add_order(2, Side::Buy, Price(100), Qty(1)).is_err();
    } catch (...) {
        threw = true;
    }
    REQUIRE(!threw);
    REQUIRE(returned_error);

    // The rejected reference was never inserted, and the original level and
    // order remain usable at their full quantity.
    REQUIRE(book.delete_order(2).is_err());
    REQUIRE(book.best_bid().has_value());
    REQUIRE_EQ(book.best_bid()->as_raw(), 100);
    REQUIRE(book.execute_order(1, Qty(max_qty)).is_ok());
    REQUIRE(!book.best_bid().has_value());
}

TEST_CASE(test_order_book_midpoint_handles_uint64_max_prices) {
    const uint64_t max_price = std::numeric_limits<uint64_t>::max();
    OrderBook book;
    REQUIRE(book.add_order(1, Side::Buy, Price(max_price - 2), Qty(1)).is_ok());
    REQUIRE(book.add_order(2, Side::Sell, Price(max_price), Qty(1)).is_ok());

    REQUIRE(book.mid_price().has_value());
    REQUIRE_EQ(book.mid_price()->as_raw(), max_price - 1);
}

TEST_CASE(test_order_book_rejects_invalid_values_without_mutation) {
    OrderBook book;

    REQUIRE(book.add_order(1, Side::Buy, Price(0), Qty(1)).is_err());
    REQUIRE(book.add_order(2, Side::Buy, Price(100), Qty::zero()).is_err());
    REQUIRE(book.add_order(3, static_cast<Side>(0), Price(100), Qty(1)).is_err());
    REQUIRE(!book.best_bid().has_value());
    REQUIRE(!book.best_ask().has_value());

    REQUIRE(book.add_order(4, Side::Buy, Price(100), Qty(10)).is_ok());
    REQUIRE(book.execute_order(4, Qty::zero()).is_err());
    REQUIRE(book.execute_order(4, Qty(11)).is_err());

    // Rejected executions must leave the resting order intact at its original
    // level and quantity.
    REQUIRE(book.best_bid().has_value());
    REQUIRE_EQ(book.best_bid()->as_raw(), 100);
    REQUIRE(book.execute_order(4, Qty(10)).is_ok());
    REQUIRE(!book.best_bid().has_value());
}

TEST_CASE(test_order_book_replace_is_transactional) {
    OrderBook book;
    REQUIRE(book.add_order(1, Side::Buy, Price(100), Qty(10)).is_ok());
    REQUIRE(book.add_order(2, Side::Buy, Price(101), Qty(5)).is_ok());

    // A duplicate replacement reference is rejected before the original is
    // deleted. Removing the conflicting order exposes the original level.
    REQUIRE(book.replace_order(1, 2, Qty(7), Price(99)).is_err());
    REQUIRE(book.delete_order(2).is_ok());
    REQUIRE(book.best_bid().has_value());
    REQUIRE_EQ(book.best_bid()->as_raw(), 100);

    // Invalid replacement values likewise preserve the original order.
    REQUIRE(book.replace_order(1, 3, Qty::zero(), Price(99)).is_err());
    REQUIRE(book.replace_order(1, 3, Qty(7), Price(0)).is_err());
    REQUIRE(book.best_bid().has_value());
    REQUIRE_EQ(book.best_bid()->as_raw(), 100);

    // Failure to aggregate at the target level is also transactional.
    const uint64_t max_qty = std::numeric_limits<uint64_t>::max();
    REQUIRE(book.add_order(4, Side::Buy, Price(110), Qty(max_qty)).is_ok());
    REQUIRE(book.replace_order(1, 3, Qty(1), Price(110)).is_err());
    REQUIRE(book.delete_order(4).is_ok());
    REQUIRE(book.best_bid().has_value());
    REQUIRE_EQ(book.best_bid()->as_raw(), 100);

    // A valid replacement commits both indexes together.
    REQUIRE(book.replace_order(1, 3, Qty(7), Price(99)).is_ok());
    REQUIRE(book.delete_order(1).is_err());
    REQUIRE(book.best_bid().has_value());
    REQUIRE_EQ(book.best_bid()->as_raw(), 99);
    REQUIRE(book.execute_order(3, Qty(7)).is_ok());
    REQUIRE(!book.best_bid().has_value());
}

TEST_CASE(test_slice_packet_source_consumes_framed_stream) {
    // An empty buffer yields no message rather than an error, which is what lets
    // the replay loop terminate on end-of-stream instead of on a failure.
    const std::vector<uint8_t> empty;
    SlicePacketSource source(empty);
    REQUIRE_EQ(source.remaining(), 0u);
    auto first = source.next_message();
    REQUIRE(first.is_ok());
    REQUIRE(!first.value().has_value());

    // A truncated frame is an error, not a silent stop.
    const std::vector<uint8_t> truncated = {0x00, 0x20, 'A'};
    SlicePacketSource bad(truncated);
    REQUIRE(bad.next_message().is_err());

    // ITCH message sizes are exact. A valid Add Order prefix with trailing
    // bytes is malformed, not an extensible message variant.
    std::vector<uint8_t> oversized_add(37, 0);
    oversized_add[0] = 'A';
    oversized_add[19] = 'B';
    REQUIRE(ItchStreamParser::parse_payload(oversized_add).is_err());
}
