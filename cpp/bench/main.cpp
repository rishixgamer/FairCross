// FairCross C++ benchmark runner.
//
// Built without sanitizers at -O2; see bench/harness.hpp for why that matters.
// Writes artifacts under experiments/results/, each stamped with the
// environment and reproduction command that produced it.

#include <iostream>
#include <string>

namespace faircross::bench {
void run_proof_scaling();
void run_accounting_metrics();
void run_clearing_optimality_metrics();
void run_two_step_ivc_spike();
void run_ten_batch_audit();
void run_recursive_vs_independent();
void run_engine_profile();
void run_auction_optimization();
void run_attack_evidence();
void run_batch_interval_matrix();
void run_proof_cost_model();
} // namespace faircross::bench

namespace {

using Bench = void (*)();

struct Entry {
    const char* name;
    Bench fn;
};

const Entry kBenchmarks[] = {
    {"proof-scaling", faircross::bench::run_proof_scaling},
    {"accounting-metrics", faircross::bench::run_accounting_metrics},
    {"clearing-optimality", faircross::bench::run_clearing_optimality_metrics},
    {"two-step-ivc", faircross::bench::run_two_step_ivc_spike},
    {"ten-batch-audit", faircross::bench::run_ten_batch_audit},
    {"recursive-vs-independent", faircross::bench::run_recursive_vs_independent},
    {"engine-profile", faircross::bench::run_engine_profile},
    {"auction-optimization", faircross::bench::run_auction_optimization},
    {"attack-evidence", faircross::bench::run_attack_evidence},
    {"batch-interval-matrix", faircross::bench::run_batch_interval_matrix},
    {"proof-cost-model", faircross::bench::run_proof_cost_model},
};

void usage(const char* prog) {
    std::cerr << "FairCross C++ benchmark runner\n\n"
              << "Usage: " << prog << " <benchmark|all>\n\n"
              << "Benchmarks:\n";
    for (const auto& e : kBenchmarks) std::cerr << "  " << e.name << "\n";
    std::cerr << "  all\n";
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }
    const std::string which = argv[1];

    try {
        if (which == "all") {
            std::cout << "Running all FairCross C++ benchmarks...\n";
            for (const auto& e : kBenchmarks) {
                std::cout << "-- " << e.name << "\n";
                e.fn();
            }
            std::cout << "All benchmarks complete.\n";
            return 0;
        }
        for (const auto& e : kBenchmarks) {
            if (which == e.name) {
                e.fn();
                return 0;
            }
        }
    } catch (const std::exception& ex) {
        std::cerr << "benchmark failed: " << ex.what() << "\n";
        return 1;
    }

    std::cerr << "Unknown benchmark: " << which << "\n";
    usage(argv[0]);
    return 1;
}
