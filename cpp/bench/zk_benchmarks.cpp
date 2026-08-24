// Proof-layer benchmarks producing the zk_* artifacts.

#include "harness.hpp"
#include "fixtures.hpp"

#include "faircross/proof/verifier.hpp"
#include "faircross/proof/recursive/prover.hpp"
#include "faircross/proof/recursive/verifier.hpp"
#include "faircross/proof/constraints.hpp"
#include "faircross/engine/auction.hpp"
#include "faircross/engine/allocation.hpp"

#include <iostream>

namespace faircross::bench {

namespace {
constexpr size_t kSamples = 5;
const std::vector<size_t> kBatchSizes = {2, 4, 8, 16, 32, 64};
} // namespace

void run_proof_scaling() {
    json::Writer w;
    w.begin_object();
    w.field_string("benchmark_name", "faircross_single_batch_proof_v0_scaling");
    w.field_string("reproduction_command", "cpp/bin/faircross_bench proof-scaling");
    write_environment(w);
    w.field_u64("samples_per_point", kSamples);
    w.key("data_points");
    w.begin_array();

    for (const size_t batch_size : kBatchSizes) {
        const Ledger pre_state = funded_ledger(InstrumentId(1), batch_size);
        const ProofFixture fx = build_proof_fixture(batch_size, pre_state);

        // Warm the caches once so the first sample is not an outlier.
        (void)SingleBatchProver::prove(fx.public_inputs, fx.witness);

        std::vector<uint64_t> prove_us;
        std::vector<uint64_t> verify_us;
        BatchProof proof{};
        for (size_t i = 0; i < kSamples; ++i) {
            Timer t;
            auto res = SingleBatchProver::prove(fx.public_inputs, fx.witness);
            prove_us.push_back(t.elapsed_us());
            proof = res.value();

            Timer v;
            (void)SingleBatchVerifier::verify(fx.public_inputs, proof);
            verify_us.push_back(v.elapsed_us());
        }

        // Replay the full synthesis to report the whole system's variable
        // counts. Counting one subrelation would understate them.
        const auto [constraints, publics, witnesses] = synthesize_full_system(fx);

        w.begin_object();
        w.field_u64("batch_size", batch_size);
        w.field_u64("num_constraints", constraints);
        w.field_u64("num_public_variables", publics);
        w.field_u64("num_witness_variables", witnesses);
        w.field_u64("proof_size_bytes", proof.proof_bytes.size());
        w.field_u64("prove_time_us", median(prove_us));
        w.field_u64("verify_time_us", median(verify_us));
        w.end_object();
    }

    w.end_array();
    w.end_object();
    write_artifact("zk_proof_scaling_benchmark.json", w.str() + "\n");
    std::cout << "  wrote zk_proof_scaling_benchmark.json\n";
}

void run_accounting_metrics() {
    json::Writer w;
    w.begin_object();
    w.field_string("benchmark_name", "zk_complete_input_accounting_r1cs");
    w.field_string("reproduction_command", "cpp/bin/faircross_bench accounting-metrics");
    write_environment(w);
    w.key("runs");
    w.begin_array();

    for (const size_t batch_size : kBatchSizes) {
        const Ledger pre_state = funded_ledger(InstrumentId(1), batch_size);
        const ProofFixture fx = build_proof_fixture(batch_size, pre_state);

        std::vector<uint64_t> us;
        size_t constraints = 0, publics = 0, witnesses = 0;
        for (size_t i = 0; i < kSamples; ++i) {
            ConstraintSystem cs;
            std::vector<uint64_t> scalars;
            Timer t;
            auto res = synthesize_complete_input_accounting_constraints(
                cs, fx.witness.batch, fx.witness.preimages,
                MerkleTree([&] {
                    std::vector<Commitment> leaves;
                    for (const auto& p : fx.witness.preimages) leaves.push_back(commit_order(p));
                    return leaves;
                }()),
                fx.witness.accounting, fx.witness.allocation, scalars);
            us.push_back(t.elapsed_us());
            if (res.is_err()) {
                std::cerr << "accounting synthesis failed: " << res.error_message() << "\n";
                std::exit(1);
            }
            constraints = cs.num_constraints();
            publics = cs.num_public();
            witnesses = cs.num_witness();
        }

        w.begin_object();
        w.field_u64("batch_size", batch_size);
        w.field_u64("num_constraints", constraints);
        w.field_u64("num_public_variables", publics);
        w.field_u64("num_witness_variables", witnesses);
        w.field_u64("synthesis_and_verify_us", median(us));
        w.end_object();
    }

    w.end_array();
    w.end_object();
    write_artifact("zk_accounting_metrics.json", w.str() + "\n");
    std::cout << "  wrote zk_accounting_metrics.json\n";
}

void run_clearing_optimality_metrics() {
    json::Writer w;
    w.begin_object();
    w.field_string("benchmark_name", "zk_clearing_optimality_r1cs");
    w.field_string("reproduction_command", "cpp/bin/faircross_bench clearing-optimality");
    write_environment(w);
    w.key("runs");
    w.begin_array();

    // This benchmark has its own size list and price ladder; see optimality_orders.
    for (const size_t batch_size : {size_t{2}, size_t{4}, size_t{8}, size_t{16}, size_t{32}}) {
        const InstrumentId inst(1);
        Batch batch(BatchId(1), inst, optimality_orders(batch_size, inst));
        auto outcome = determine_clearing_outcome(batch).value();

        std::vector<uint64_t> us;
        ProofComplexityMetrics metrics{0, 0, 0, 0};
        for (size_t i = 0; i < kSamples; ++i) {
            ConstraintSystem cs;
            std::vector<uint64_t> scalars;
            Timer t;
            auto res = synthesize_clearing_optimality_constraints(
                cs, batch, outcome.clearing_price, outcome.executable_volume, scalars);
            us.push_back(t.elapsed_us());
            metrics = res.value();
        }

        w.begin_object();
        w.field_u64("batch_size", batch_size);
        w.field_u64("num_candidate_prices", metrics.num_candidate_prices);
        w.field_u64("num_constraints", metrics.num_constraints);
        w.field_u64("num_public_variables", metrics.num_public_variables);
        w.field_u64("num_witness_variables", metrics.num_witness_variables);
        w.field_u64("synthesis_and_verify_us", median(us));
        w.end_object();
    }

    w.end_array();
    w.end_object();
    write_artifact("zk_clearing_optimality_metrics.json", w.str() + "\n");
    std::cout << "  wrote zk_clearing_optimality_metrics.json\n";
}

} // namespace faircross::bench
