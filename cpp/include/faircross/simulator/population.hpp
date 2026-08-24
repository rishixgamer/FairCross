#pragma once

#include <vector>
#include "faircross/domain/ledger.hpp"
#include "faircross/simulator/agent.hpp"
#include "faircross/simulator/generator.hpp"

namespace faircross {

struct PopulationSimulationConfig {
    uint64_t seed;
    InstrumentId instrument_id;
    Price initial_mid_price;
    std::vector<SyntheticMarketMaker> market_makers;
    std::vector<SyntheticNoiseTrader> noise_traders;
    Money initial_cash;
    Qty initial_inventory;
    size_t num_batches;
    size_t taker_orders_per_batch;
};

struct PopulationSession {
    PopulationSimulationConfig config;
    Ledger genesis_ledger;
    std::vector<SyntheticBatchBundle> batches;
};

class AgentPopulationSimulator {
public:
    static PopulationSession simulate_session(const PopulationSimulationConfig& config);
};

} // namespace faircross
