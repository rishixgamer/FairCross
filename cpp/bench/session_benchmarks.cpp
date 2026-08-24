// Recursive-session benchmarks producing the IVC and audit artifacts.

#include "harness.hpp"
#include "fixtures.hpp"

#include "faircross/proof/verifier.hpp"
#include "faircross/proof/recursive/prover.hpp"
#include "faircross/proof/recursive/verifier.hpp"

#include <iostream>

namespace faircross::bench {

namespace {

/// Builds one folded step from two crossing orders.
RecursiveStep build_ivc_step(uint64_t batch_seq, const Ledger& prev, uint64_t price,
                             uint64_t qty, Ledger& out_post) {
    const InstrumentId inst(1);
    Batch batch(BatchId(batch_seq), inst, ivc_step_orders(batch_seq, price, qty, inst));

    std::vector<SaltedOrderPreimage> preimages;
    for (size_t i = 0; i < batch.orders().size(); ++i) {
        std::array<uint8_t, 32> nonce{};
        nonce.fill(static_cast<uint8_t>(batch_seq * 10 + i + 1));
        preimages.emplace_back(batch.orders()[i], nonce);
    }

    auto exec = execute_batch(prev, batch).value();
    auto [tree, accounting] = build_complete_input_accounting(batch, preimages, exec.allocation);

    const uint64_t ts = 1'700'000'000ULL + batch_seq * 10;
    BatchHeader header(1, batch.batch_id(), inst, ts, static_cast<uint32_t>(batch.len()),
                       tree.root());

    BatchProofPublicInputs pi{
        compute_ledger_root(prev, inst),
        compute_ledger_root(exec.post_state, inst),
        compute_batch_commitment(header),
        compute_oracle_snapshot_commitment(std::nullopt, std::nullopt),
        exec.clearing_outcome.clearing_price.value().as_raw(),
        exec.clearing_outcome.executable_volume.as_raw()};

    BatchProofWitness wit{prev,            exec.post_state, batch,     preimages,
                          exec.allocation, exec.fills,      accounting, std::nullopt,
                          std::nullopt,    ts};

    out_post = exec.post_state;
    return RecursiveStep{pi, wit, ts};
}

/// Session of two-order batches at a fixed price. The batch shape is
/// deliberately not the scaling ladder: those benchmarks measure different
/// things and their constraint counts differ.
std::pair<RunningState, std::vector<RecursiveStep>> build_session(size_t count,
                                                                  size_t /*batch_size*/) {
    const InstrumentId inst(1);
    Ledger ledger = funded_ledger(inst, 4);
    const RunningState initial = RunningState::genesis(compute_ledger_root(ledger, inst), inst);

    std::vector<RecursiveStep> steps;
    steps.reserve(count);
    for (size_t i = 1; i <= count; ++i) {
        Ledger post;
        steps.push_back(build_ivc_step(static_cast<uint64_t>(i), ledger, 100, 20, post));
        ledger = post;
    }
    return {initial, steps};
}

} // namespace

void run_two_step_ivc_spike() {
    auto [initial, steps] = build_session(2, 2);

    Timer t1;
    auto s1 = initial.fold_step(steps[0].public_inputs, steps[0].witness, steps[0].timestamp_nanos);
    const uint64_t step_1_us = t1.elapsed_us();
    if (s1.is_err()) {
        std::cerr << "step 1 fold failed: " << s1.error_message() << "\n";
        std::exit(1);
    }

    Timer t2;
    auto s2 = s1.value().fold_step(steps[1].public_inputs, steps[1].witness,
                                   steps[1].timestamp_nanos);
    const uint64_t step_2_us = t2.elapsed_us();
    if (s2.is_err()) {
        std::cerr << "step 2 fold failed: " << s2.error_message() << "\n";
        std::exit(1);
    }

    auto session = RecursiveSessionProver::prove_session(initial, steps).value();
    Timer v;
    auto verdict = RecursiveSessionVerifier::verify_session(session.public_inputs, session);
    const uint64_t verify_us = v.elapsed_us();
    if (verdict.is_err()) {
        std::cerr << "session verify failed: " << verdict.error_message() << "\n";
        std::exit(1);
    }

    json::Writer w;
    w.begin_object();
    w.field_string("benchmark_name", "faircross_two_step_ivc_spike");
    w.field_string("reproduction_command", "cpp/bin/faircross_bench two-step-ivc");
    write_environment(w);
    w.field_u64("step_1_prove_us", step_1_us);
    w.field_u64("step_2_prove_us", step_2_us);
    w.field_u64("total_prove_us", step_1_us + step_2_us);
    w.field_u64("verify_us", verify_us);
    w.field_u64("step_1_constraints", constraints_of(session.step_proofs[0]));
    w.field_u64("step_2_constraints", constraints_of(session.step_proofs[1]));
    w.field_string("history_accumulator_final",
                   session.public_inputs.final_state.history_accumulator.to_hex());
    w.end_object();
    write_artifact("zk_two_step_ivc_spike.json", w.str() + "\n");
    std::cout << "  wrote zk_two_step_ivc_spike.json\n";
}

void run_ten_batch_audit() {
    auto [initial, steps] = build_session(10, 2);

    Timer t;
    auto session_res = RecursiveSessionProver::prove_session(initial, steps);
    const uint64_t total_prove_us = t.elapsed_us();
    if (session_res.is_err()) {
        std::cerr << "session prove failed: " << session_res.error_message() << "\n";
        std::exit(1);
    }
    const SessionProof& session = session_res.value();

    Timer v;
    auto verdict = RecursiveSessionVerifier::verify_session(session.public_inputs, session);
    const uint64_t total_verify_us = v.elapsed_us();
    if (verdict.is_err()) {
        std::cerr << "session verify failed: " << verdict.error_message() << "\n";
        std::exit(1);
    }

    json::Writer w;
    w.begin_object();
    w.field_string("benchmark_name", "faircross_ten_batch_running_audit");
    w.field_string("reproduction_command", "cpp/bin/faircross_bench ten-batch-audit");
    write_environment(w);
    w.field_u64("num_batches", session.num_batches);
    w.field_u64("total_prove_time_us", total_prove_us);
    w.field_u64("mean_prove_time_per_batch_us", total_prove_us / session.num_batches);
    w.field_u64("total_verify_time_us", total_verify_us);
    w.field_u64("proof_certificate_bytes", session.proof_certificate.size());
    w.field_string("final_history_accumulator",
                   session.public_inputs.final_state.history_accumulator.to_hex());
    w.field_u64("final_batch_id", session.public_inputs.final_state.batch_id.as_raw());
    w.end_object();
    write_artifact("zk_ten_batch_audit_metrics.json", w.str() + "\n");
    std::cout << "  wrote zk_ten_batch_audit_metrics.json\n";
}

void run_recursive_vs_independent() {
    const std::vector<size_t> lengths = {1, 2, 5, 10, 20, 50};
    constexpr size_t kSamples = 5;

    json::Writer w;
    w.begin_object();
    w.field_string("benchmark_name", "faircross_recursive_vs_independent_history");
    w.field_string("reproduction_command", "cpp/bin/faircross_bench recursive-vs-independent");
    write_environment(w);
    w.field_u64("samples_per_point", kSamples);
    w.key("results");
    w.begin_array();

    for (const size_t length : lengths) {
        auto [initial, steps] = build_session(length, 2);

        // Independent: every step proof stored and verified separately.
        std::vector<uint64_t> ind_prove;
        std::vector<uint64_t> ind_verify;
        size_t independent_bytes = 0;
        for (size_t s = 0; s < kSamples; ++s) {
            std::vector<BatchProof> proofs;
            Timer p;
            for (const auto& step : steps) {
                proofs.push_back(
                    SingleBatchProver::prove(step.public_inputs, step.witness).value());
            }
            ind_prove.push_back(p.elapsed_us());

            Timer v;
            for (const auto& proof : proofs) {
                (void)SingleBatchVerifier::verify(proof.public_inputs, proof);
            }
            ind_verify.push_back(v.elapsed_us());

            independent_bytes = 0;
            for (const auto& proof : proofs) independent_bytes += proof.proof_bytes.size();
        }

        // Recursive: one folded session certificate.
        std::vector<uint64_t> rec_prove;
        std::vector<uint64_t> rec_verify;
        size_t recursive_bytes = 0;
        for (size_t s = 0; s < kSamples; ++s) {
            Timer p;
            auto session = RecursiveSessionProver::prove_session(initial, steps).value();
            rec_prove.push_back(p.elapsed_us());

            Timer v;
            (void)RecursiveSessionVerifier::verify_session(session.public_inputs, session);
            rec_verify.push_back(v.elapsed_us());

            recursive_bytes = session.proof_certificate.size();
        }

        w.begin_object();
        w.field_u64("session_length_batches", length);
        w.field_u64("independent_total_proof_size_bytes", independent_bytes);
        w.field_u64("independent_total_prove_time_us", median(ind_prove));
        w.field_u64("independent_total_verify_time_us", median(ind_verify));
        w.field_u64("recursive_certificate_size_bytes", recursive_bytes);
        w.field_u64("recursive_total_prove_time_us", median(rec_prove));
        w.field_u64("recursive_total_verify_time_us", median(rec_verify));
        // Reported as a permille integer: the project forbids floating point in
        // exchange state, and a ratio in an artifact is easier to diff exactly.
        w.field_u64("storage_compression_ratio_permille",
                    recursive_bytes == 0
                        ? 0
                        : (independent_bytes * 1000) / recursive_bytes);
        w.end_object();
    }

    w.end_array();
    w.end_object();
    write_artifact("zk_recursive_vs_independent_comparison.json", w.str() + "\n");
    std::cout << "  wrote zk_recursive_vs_independent_comparison.json\n";
}

} // namespace faircross::bench
