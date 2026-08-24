// Plaintext engine and adversarial-evidence benchmarks.

#include "harness.hpp"
#include "fixtures.hpp"

#include "faircross/engine/auction.hpp"
#include "faircross/engine/checker.hpp"
#include "faircross/adversary/matrix.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>

namespace faircross::bench {

namespace {
constexpr size_t kIterations = 100;
const std::vector<size_t> kProfileSizes = {10, 50, 100, 250, 500};

std::string fixed2(double v) {
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss.precision(3);
    oss << v;
    return oss.str();
}

/// Deliberately naive O(N^2) candidate-price evaluation, kept only in the
/// benchmark harness as the baseline for the optimization comparison. It is not
/// part of the engine and is never linked into the product binary.
Qty naive_max_volume(const std::vector<Order>& orders) {
    Qty best = Qty::zero();
    for (const Order& candidate : orders) {
        uint64_t demand = 0;
        uint64_t supply = 0;
        for (const Order& o : orders) {
            if (o.side == Side::Buy && o.price.as_raw() >= candidate.price.as_raw()) {
                demand += o.qty.as_raw();
            }
            if (o.side == Side::Sell && o.price.as_raw() <= candidate.price.as_raw()) {
                supply += o.qty.as_raw();
            }
        }
        const uint64_t v = std::min(demand, supply);
        if (v > best.as_raw()) best = Qty(v);
    }
    return best;
}
} // namespace

void run_engine_profile() {
    json::Writer w;
    w.begin_object();
    w.field_string("benchmark_name", "faircross_plaintext_engine_profile");
    w.field_string("reproduction_command", "cpp/bin/faircross_bench engine-profile");
    write_environment(w);
    w.field_u64("iterations_per_point", kIterations);
    w.key("results");
    w.begin_array();

    std::ostringstream csv;
    csv << "batch_size,iterations,mean_execution_micros,p50_execution_micros,"
        << "p90_execution_micros,p99_execution_micros,mean_verification_micros,"
        << "throughput_orders_per_sec\n";

    for (const size_t batch_size : kProfileSizes) {
        const InstrumentId inst(1);
        // The ledger must be funded for the same account count the orders span:
        // scaling_orders spreads across batch_size/2 accounts, so taking the
        // default here leaves every account above the second unfunded and
        // execute_batch fails for any size past 4.
        const Ledger pre_state = funded_ledger(inst, batch_size);
        Batch batch(BatchId(1), inst, scaling_orders(batch_size, inst));

        std::vector<uint64_t> exec_us;
        std::vector<uint64_t> verify_us;
        for (size_t i = 0; i < kIterations; ++i) {
            Timer t;
            auto exec = execute_batch(pre_state, batch);
            exec_us.push_back(t.elapsed_us());

            Timer v;
            (void)verify_transition(pre_state, batch, exec.value());
            verify_us.push_back(v.elapsed_us());
        }

        const double mean_exec = mean(exec_us);
        const double throughput =
            mean_exec > 0.0 ? (static_cast<double>(batch_size) * 1'000'000.0) / mean_exec : 0.0;

        w.begin_object();
        w.field_u64("batch_size", batch_size);
        w.field_u64("iterations", kIterations);
        w.field_string("mean_execution_micros", fixed2(mean_exec));
        w.field_u64("p50_execution_micros", percentile(exec_us, 0.50));
        w.field_u64("p90_execution_micros", percentile(exec_us, 0.90));
        w.field_u64("p99_execution_micros", percentile(exec_us, 0.99));
        w.field_string("mean_verification_micros", fixed2(mean(verify_us)));
        w.field_string("throughput_orders_per_sec", fixed2(throughput));
        w.end_object();

        csv << batch_size << "," << kIterations << "," << fixed2(mean_exec) << ","
            << percentile(exec_us, 0.50) << "," << percentile(exec_us, 0.90) << ","
            << percentile(exec_us, 0.99) << "," << fixed2(mean(verify_us)) << ","
            << fixed2(throughput) << "\n";
    }

    w.end_array();
    w.end_object();
    write_artifact("plaintext_engine_profile.json", w.str() + "\n");
    write_artifact("plaintext_engine_profile.csv", csv.str());
    std::cout << "  wrote plaintext_engine_profile.{json,csv}\n";
}

void run_auction_optimization() {
    json::Writer w;
    w.begin_object();
    w.field_string("benchmark_name", "faircross_auction_optimization_comparison");
    w.field_string("reproduction_command", "cpp/bin/faircross_bench auction-optimization");
    write_environment(w);
    w.field_string("hotspot_description", "Volume maximizer candidate price evaluation");
    w.field_string(
        "algorithmic_change",
        "Naive O(N^2) repeated order scans versus the engine's candidate-price evaluation");
    w.field_string("baseline_note",
                   "The naive variant lives only in bench/engine_benchmarks.cpp as a reference "
                   "baseline; it is not part of the engine and is never linked into the product");
    w.key("results");
    w.begin_array();

    for (const size_t batch_size : {20u, 50u, 100u, 200u}) {
        const InstrumentId inst(1);
        const auto orders = scaling_orders(batch_size, inst);
        Batch batch(BatchId(1), inst, orders);

        std::vector<uint64_t> naive_us;
        std::vector<uint64_t> opt_us;
        Qty naive_result = Qty::zero();
        Qty opt_result = Qty::zero();

        for (size_t i = 0; i < kIterations; ++i) {
            Timer t1;
            naive_result = naive_max_volume(orders);
            naive_us.push_back(t1.elapsed_us());

            Timer t2;
            opt_result = determine_clearing_outcome(batch).value().executable_volume;
            opt_us.push_back(t2.elapsed_us());
        }

        const double naive_mean = mean(naive_us);
        const double opt_mean = mean(opt_us);

        w.begin_object();
        w.field_u64("batch_size", batch_size);
        w.field_u64("iterations", kIterations);
        w.field_string("unoptimized_mean_micros", fixed2(naive_mean));
        w.field_string("optimized_mean_micros", fixed2(opt_mean));
        w.field_string("speedup_factor", fixed2(opt_mean > 0.0 ? naive_mean / opt_mean : 0.0));
        w.field_bool("differential_equivalence_verified", naive_result == opt_result);
        w.end_object();
    }

    w.end_array();
    w.end_object();
    write_artifact("auction_optimization_comparison.json", w.str() + "\n");
    std::cout << "  wrote auction_optimization_comparison.json\n";
}

void run_attack_evidence() {
    const InstrumentId inst(1);
    const Ledger pre_state = funded_ledger(inst);
    Batch batch(BatchId(1), inst, scaling_orders(4, inst));
    const auto preimages = preimages_for(batch);
    const auto report = run_attack_matrix(pre_state, batch, preimages);

    json::Writer w;
    w.begin_object();
    w.field_string("benchmark_name", "faircross_adversarial_attack_evidence");
    w.field_string("reproduction_command", "cpp/bin/faircross_bench attack-evidence");
    write_environment(w);
    w.field_u64("total_vectors_evaluated", report.total_evaluated);
    w.field_u64("malicious_attacks_rejected", report.total_attacks_rejected);
    w.field_bool("all_invariants_held", report.all_invariants_held);
    w.key("entries");
    w.begin_array();

    std::ostringstream csv;
    csv << "attack_name,is_honest_control,passed_verification,rejection_reason\n";

    for (const auto& r : report.results) {
        w.begin_object();
        w.field_string("attack_name", r.attack_name);
        w.field_string("description", r.description);
        w.field_bool("is_honest_control", r.is_honest_control);
        w.field_bool("passed_verification", r.passed_verification);
        w.field_string("rejection_reason", r.rejection_reason.value_or(""));
        w.end_object();

        std::string reason = r.rejection_reason.value_or("");
        std::replace(reason.begin(), reason.end(), ',', ';');
        csv << r.attack_name << "," << (r.is_honest_control ? "true" : "false") << ","
            << (r.passed_verification ? "true" : "false") << "," << reason << "\n";
    }

    w.end_array();
    w.end_object();
    write_artifact("attack_evidence_table.json", w.str() + "\n");
    write_artifact("attack_evidence_table.csv", csv.str());
    std::cout << "  wrote attack_evidence_table.{json,csv}\n";
}

} // namespace faircross::bench
