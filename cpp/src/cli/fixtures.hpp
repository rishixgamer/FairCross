#pragma once

// Fixture and manifest loading for the FairCross CLI.
//
// Reads the batch and session fixtures under `fixtures/`, which are the same
// inputs the frozen golden expectations were recorded from.

#include <string>
#include <vector>

#include "faircross/util/json.hpp"
#include "faircross/domain/ledger.hpp"
#include "faircross/engine/batch.hpp"

namespace faircross::cli {

struct BatchFixture {
    Ledger pre_state;
    Batch batch;
};

struct SessionManifest {
    Ledger genesis_ledger;
    std::vector<Batch> batches;
};

inline Side side_from_string(const std::string& s) {
    if (s == "buy") return Side::Buy;
    if (s == "sell") return Side::Sell;
    throw std::runtime_error("unknown side '" + s + "'");
}

inline Ledger ledger_from_json(const json::Value& accounts) {
    Ledger ledger;
    for (const json::Value& acc : accounts.array()) {
        AccountState state(AccountId(acc.at("account_id").as_u64()),
                           Money::from_raw(static_cast<Money::RawType>(acc.at("cash").as_u64())));
        for (const auto& [inst_str, qty] : acc.at("inventory").object()) {
            // A zero entry is retained rather than skipped: the fixture's key set
            // is meaningful, and post-state output must agree with it key for key.
            const auto res = state.credit_inventory(
                InstrumentId(static_cast<uint64_t>(std::stoull(inst_str))),
                Qty(qty.as_u64()));
            if (res.is_err()) throw std::runtime_error("invalid inventory in fixture");
        }
        ledger.insert_account(std::move(state));
    }
    return ledger;
}

inline Batch batch_from_json(const json::Value& doc) {
    std::vector<Order> orders;
    for (const json::Value& o : doc.at("orders").array()) {
        auto price = Price::create(o.at("price").as_u64());
        if (price.is_err()) throw std::runtime_error(price.error_message());
        auto qty = Qty::create(o.at("qty").as_u64());
        if (qty.is_err()) throw std::runtime_error(qty.error_message());
        orders.push_back(Order{
            OrderId(o.at("id").as_u64()),
            AccountId(o.at("account").as_u64()),
            InstrumentId(o.at("instrument").as_u64()),
            side_from_string(o.at("side").as_string()),
            price.value(),
            qty.value(),
            o.at("seq").as_u64(),
        });
    }
    auto batch = Batch::create(BatchId(doc.at("batch_id").as_u64()),
                               InstrumentId(doc.at("instrument").as_u64()),
                               std::move(orders));
    if (batch.is_err()) throw std::runtime_error(batch.error_message());
    return batch.value();
}

inline BatchFixture load_batch_fixture(const std::string& path) {
    const json::Value doc = json::parse_file(path);
    return BatchFixture{ledger_from_json(doc.at("pre_state")), batch_from_json(doc.at("batch"))};
}

inline SessionManifest load_session_manifest(const std::string& path) {
    const json::Value doc = json::parse_file(path);
    SessionManifest manifest{ledger_from_json(doc.at("genesis_ledger")), {}};
    for (const json::Value& b : doc.at("batches").array()) {
        manifest.batches.push_back(batch_from_json(b));
    }
    return manifest;
}

} // namespace faircross::cli
