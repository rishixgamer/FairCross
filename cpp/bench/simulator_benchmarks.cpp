// Simulator benchmarks: batch-interval sweep and the proof cost model.

#include "harness.hpp"
#include "fixtures.hpp"

#include "faircross/simulator/generator.hpp"
#include "faircross/simulator/interval.hpp"
#include "faircross/simulator/metrics.hpp"
#include "faircross/simulator/rng.hpp"
#include "faircross/engine/auction.hpp"

#include <iostream>
#include <sstream>

namespace faircross::bench {

namespace {

const std::vector<uint64_t> kIntervalsMs = {10, 50, 100, 250, 500};
const std::vector<uint64_t> kSeeds = {101, 202, 303};
constexpr uint64_t kHorizonSeconds = 10;

std::string fixed3(double v) {
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss.precision(3);
    oss << v;
    return oss.str();
}

struct IntervalRun {
    uint64_t seed;
    uint64_t interval_ms;
    size_t total_batches;
    size_t total_submitted_orders;
    size_t total_fills;
    uint64_t total_submitted_volume;
    uint64_t total_cleared_volume;
    uint32_t fill_rate_permille;
};

/// One synthetic session partitioned at a given batch interval.
IntervalRun evaluate(uint64_t seed, uint64_t interval_ms) {
    const InstrumentId inst(1);
    SyntheticFlowConfig config;
    config.seed = seed;
    config.instrument_id = inst;
    config.num_batches = 40;
    config.min_orders_per_batch = 4;
    config.max_orders_per_batch = 10;

    const SyntheticSession session = SyntheticOrderFlowSimulator::generate_session(config);

    // Flatten the generated batches into a timestamped arrival stream, then
    // re-partition at the interval under test.
    DeterministicRng rng(seed ^ 0x5DEECE66DULL);
    std::vector<TimestampedOrderSubmission> submissions;
    const uint64_t horizon_nanos = kHorizonSeconds * 1'000'000'000ULL;
    const uint64_t start = 1'700'000'000'000'000'000ULL;

    for (const auto& bundle : session.batches) {
        for (size_t i = 0; i < bundle.batch.orders().size(); ++i) {
            submissions.push_back(TimestampedOrderSubmission{
                bundle.batch.orders()[i],
                start + rng.gen_range_u64(0, horizon_nanos),
                bundle.preimages[i].nonce,
            });
        }
    }

    const OrderInputStream stream(inst, std::move(submissions));
    const auto batches = BatchIntervalPartitionEngine::partition_stream(
        stream, start, interval_ms * 1'000'000ULL, std::nullopt);

    Ledger ledger = funded_ledger(inst);
    IntervalRun run{seed, interval_ms, batches.size(), 0, 0, 0, 0, 0};

    for (const auto& bundle : batches) {
        if (bundle.batch.is_empty()) continue;
        run.total_submitted_orders += bundle.batch.len();
        for (const Order& o : bundle.batch.orders()) {
            run.total_submitted_volume += o.qty.as_raw();
        }
        auto exec_res = execute_batch(ledger, bundle.batch);
        if (exec_res.is_err()) continue;
        const auto& exec = exec_res.value();
        run.total_fills += exec.fills.size();
        run.total_cleared_volume += exec.clearing_outcome.executable_volume.as_raw();
        ledger = exec.post_state;
    }

    run.fill_rate_permille =
        run.total_submitted_volume == 0
            ? 0
            : static_cast<uint32_t>((run.total_cleared_volume * 1000) / run.total_submitted_volume);
    return run;
}

} // namespace

std::vector<IntervalRun> collect_interval_runs() {
    std::vector<IntervalRun> runs;
    for (const uint64_t seed : kSeeds) {
        for (const uint64_t interval : kIntervalsMs) {
            runs.push_back(evaluate(seed, interval));
        }
    }
    return runs;
}

void run_batch_interval_matrix() {
    const auto runs = collect_interval_runs();

    json::Writer w;
    w.begin_object();
    w.field_string("experiment_name", "faircross_batch_interval_matrix");
    w.field_string("reproduction_command", "cpp/bin/faircross_bench batch-interval-matrix");
    write_environment(w);
    w.field_u64("horizon_seconds", kHorizonSeconds);
    w.key("intervals_evaluated_ms");
    w.begin_array();
    for (const uint64_t i : kIntervalsMs) w.value_u64(i);
    w.end_array();
    w.key("seeds_evaluated");
    w.begin_array();
    for (const uint64_t s : kSeeds) w.value_u64(s);
    w.end_array();
    w.key("raw_runs");
    w.begin_array();

    std::ostringstream csv;
    csv << "seed,interval_ms,total_batches,total_submitted_orders,total_fills,"
        << "total_submitted_volume,total_cleared_volume,fill_rate_permille\n";

    for (const auto& r : runs) {
        w.begin_object();
        w.field_u64("seed", r.seed);
        w.field_u64("interval_ms", r.interval_ms);
        w.field_u64("interval_nanos", r.interval_ms * 1'000'000ULL);
        w.field_u64("total_batches", r.total_batches);
        w.field_u64("total_submitted_orders", r.total_submitted_orders);
        w.field_u64("total_fills", r.total_fills);
        w.field_u64("total_submitted_volume", r.total_submitted_volume);
        w.field_u64("total_cleared_volume", r.total_cleared_volume);
        w.field_u64("fill_rate_permille", r.fill_rate_permille);
        w.end_object();

        csv << r.seed << "," << r.interval_ms << "," << r.total_batches << ","
            << r.total_submitted_orders << "," << r.total_fills << ","
            << r.total_submitted_volume << "," << r.total_cleared_volume << ","
            << r.fill_rate_permille << "\n";
    }

    w.end_array();
    w.end_object();
    write_artifact("batch_interval_matrix_raw.json", w.str() + "\n");
    write_artifact("batch_interval_matrix_raw.csv", csv.str());
    std::cout << "  wrote batch_interval_matrix_raw.{json,csv}\n";
}

void run_proof_cost_model() {
    const auto runs = collect_interval_runs();

    // Measured single-batch prove cost, reused across intervals.
    const Ledger pre_state = funded_ledger(InstrumentId(1));
    const ProofFixture fx = build_proof_fixture(2, pre_state);
    (void)SingleBatchProver::prove(fx.public_inputs, fx.witness);
    std::vector<uint64_t> prove_us;
    BatchProof proof{};
    for (size_t i = 0; i < 5; ++i) {
        Timer t;
        auto res = SingleBatchProver::prove(fx.public_inputs, fx.witness);
        prove_us.push_back(t.elapsed_us());
        proof = res.value();
    }
    const uint64_t single_batch_us = median(prove_us);
    const uint64_t single_batch_constraints = constraints_of(proof);

    json::Writer w;
    w.begin_object();
    w.field_string("benchmark_name", "faircross_proof_cost_model");
    w.field_string("provenance",
                   "Empirical: measured single-batch prove cost combined with the batch-interval "
                   "matrix session shapes from this same run");
    w.field_string("reproduction_command", "cpp/bin/faircross_bench proof-cost-model");
    write_environment(w);
    w.field_u64("single_batch_prove_time_us", single_batch_us);
    w.field_u64("single_batch_constraints", single_batch_constraints);
    w.key("rows");
    w.begin_array();

    std::ostringstream csv;
    csv << "interval_ms,mean_batches,mean_batch_size_orders,mean_cleared_volume_lots,"
        << "single_batch_prove_time_us,single_batch_constraints,total_session_prove_time_ms,"
        << "prover_duty_cycle_percent\n";

    for (const uint64_t interval : kIntervalsMs) {
        double batches = 0, orders = 0, cleared = 0;
        size_t n = 0;
        for (const auto& r : runs) {
            if (r.interval_ms != interval) continue;
            batches += static_cast<double>(r.total_batches);
            orders += static_cast<double>(r.total_submitted_orders);
            cleared += static_cast<double>(r.total_cleared_volume);
            ++n;
        }
        if (n == 0) continue;
        batches /= static_cast<double>(n);
        orders /= static_cast<double>(n);
        cleared /= static_cast<double>(n);

        const double total_prove_ms =
            (batches * static_cast<double>(single_batch_us)) / 1000.0;
        const double horizon_ms = static_cast<double>(kHorizonSeconds) * 1000.0;
        const double duty = (total_prove_ms / horizon_ms) * 100.0;
        const double mean_batch_size = batches > 0.0 ? orders / batches : 0.0;

        w.begin_object();
        w.field_u64("interval_ms", interval);
        w.field_string("mean_batches", fixed3(batches));
        w.field_string("mean_batch_size_orders", fixed3(mean_batch_size));
        w.field_string("mean_cleared_volume_lots", fixed3(cleared));
        w.field_u64("single_batch_prove_time_us", single_batch_us);
        w.field_u64("single_batch_constraints", single_batch_constraints);
        w.field_string("total_session_prove_time_ms", fixed3(total_prove_ms));
        w.field_string("prover_duty_cycle_percent", fixed3(duty));
        w.end_object();

        csv << interval << "," << fixed3(batches) << "," << fixed3(mean_batch_size) << ","
            << fixed3(cleared) << "," << single_batch_us << "," << single_batch_constraints << ","
            << fixed3(total_prove_ms) << "," << fixed3(duty) << "\n";
    }

    w.end_array();
    w.end_object();
    write_artifact("proof_cost_model_report.json", w.str() + "\n");
    write_artifact("proof_cost_model_table.csv", csv.str());
    std::cout << "  wrote proof_cost_model_{report.json,table.csv}\n";
}

} // namespace faircross::bench
