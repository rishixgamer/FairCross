// FairCross C++ CLI.
//
// A fixture-driven surface: every subcommand takes a file under `fixtures/` and
// can emit `--json` for scripted use. Every advertised subcommand is
// implemented, and `scripts/check.sh` exercises each one so the usage text and
// the implementation cannot drift apart.

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "faircross/domain/primitives.hpp"
#include "faircross/domain/order.hpp"
#include "faircross/domain/account.hpp"
#include "faircross/engine/batch.hpp"
#include "faircross/engine/transition.hpp"
#include "faircross/engine/accounting.hpp"
#include "faircross/engine/checker.hpp"
#include "faircross/adversary/matrix.hpp"
#include "faircross/commitments/batch_commitment.hpp"
#include "faircross/commitments/ledger_commitment.hpp"
#include "faircross/commitments/oracle_commitment.hpp"
#include "faircross/proof/prover.hpp"
#include "faircross/proof/verifier.hpp"
#include "faircross/proof/recursive/state.hpp"
#include "faircross/proof/recursive/prover.hpp"
#include "faircross/proof/recursive/verifier.hpp"
#include "faircross/simulator/generator.hpp"

#include "fixtures.hpp"
#include "render.hpp"

using namespace faircross;
using namespace faircross::cli;

namespace {

/// Venue secret used by this CLI to blind ledger commitments (ADR-086).
///
/// A fixed demo constant, so fixture runs are reproducible. A real deployment
/// supplies its own and does not publish it; publishing it makes the state root
/// brute-forceable again.
constexpr std::array<uint8_t, 32> kDemoVenueSecret = {
    'F', 'a', 'i', 'r', 'C', 'r', 'o', 's', 's', '-', 'd', 'e', 'm', 'o', '-', 'v',
    'e', 'n', 'u', 'e', '-', 's', 'e', 'c', 'r', 'e', 't', '-', '0', '0', '0', '1'};

void print_usage(const char* prog) {
    std::cerr << "FairCross Frequent-Batch Exchange CLI (C++)\n\n"
              << "Usage:\n"
              << "  " << prog << " run <fixture.json> [--json]\n"
              << "  " << prog << " attack-matrix <fixture.json> [--json]\n"
              << "  " << prog << " prove <fixture.json> [--verify] [--json]\n"
              << "  " << prog << " prove-session <manifest.json> [--verify] [--json]\n"
              << "  " << prog << " simulate [--json]\n"
              << "  " << prog << " help\n";
}

bool has_flag(int argc, char* argv[], const char* flag) {
    for (int i = 2; i < argc; ++i) {
        if (std::string(argv[i]) == flag) return true;
    }
    return false;
}

/// Per-order nonces and batch cutoff. Both are fixed rather than sampled, so a
/// fixture run reproduces the same batch header commitment every time.
std::vector<SaltedOrderPreimage> derive_preimages(const Batch& batch, size_t batch_index) {
    std::vector<SaltedOrderPreimage> preimages;
    preimages.reserve(batch.orders().size());
    for (size_t i = 0; i < batch.orders().size(); ++i) {
        std::array<uint8_t, 32> nonce{};
        nonce.fill(static_cast<uint8_t>(static_cast<uint8_t>(batch_index * 10 + i) + 1));
        preimages.emplace_back(batch.orders()[i], nonce);
    }
    return preimages;
}

int cmd_run(const std::string& path, bool json_only) {
    BatchFixture fixture = load_batch_fixture(path);
    blind_ledger(fixture.pre_state, kDemoVenueSecret);

    auto exec_res = execute_batch(fixture.pre_state, fixture.batch);
    if (exec_res.is_err()) {
        std::cerr << "Batch execution error: " << exec_res.error_message() << "\n";
        return 1;
    }
    const BatchExecutionResult& exec = exec_res.value();

    auto verify_res = verify_transition(fixture.pre_state, fixture.batch, exec);
    if (verify_res.is_err()) {
        std::cerr << "Invariant verification error: " << verify_res.error_message() << "\n";
        return 1;
    }

    json::Writer w;
    write_execution_result(w, fixture.batch, exec);

    if (!json_only) {
        std::cout << "============================================================\n"
                  << " FairCross Batch Execution Summary\n"
                  << "============================================================\n"
                  << "Batch ID:        " << fixture.batch.batch_id().as_raw() << "\n"
                  << "Instrument:      " << fixture.batch.instrument().as_raw() << "\n"
                  << "Orders in Batch: " << fixture.batch.len() << "\n";
        if (exec.clearing_outcome.clearing_price.has_value()) {
            std::cout << "Clearing Price:  " << exec.clearing_outcome.clearing_price->as_raw()
                      << " ticks\n";
        } else {
            std::cout << "Clearing Price:  None (No Trades Crossed)\n";
        }
        std::cout << "Executed Volume: " << exec.clearing_outcome.executable_volume.as_raw()
                  << " lots\n"
                  << "Generated Fills: " << exec.fills.size() << "\n"
                  << "------------------------------------------------------------\n"
                  << "Machine-Readable Output (JSON):\n";
    }
    std::cout << w.str() << "\n";
    return 0;
}

int cmd_attack_matrix(const std::string& path, bool json_only) {
    BatchFixture fixture = load_batch_fixture(path);
    blind_ledger(fixture.pre_state, kDemoVenueSecret);
    const auto preimages = derive_preimages(fixture.batch, 0);
    const auto report = run_attack_matrix(fixture.pre_state, fixture.batch, preimages);

    if (json_only) {
        json::Writer w;
        w.begin_object();
        w.field_u64("total_evaluated", report.total_evaluated);
        w.field_u64("total_attacks_rejected", report.total_attacks_rejected);
        w.field_bool("all_invariants_held", report.all_invariants_held);
        w.key("results");
        w.begin_array();
        for (const auto& r : report.results) {
            w.begin_object();
            w.field_string("attack_name", r.attack_name);
            w.field_string("description", r.description);
            w.field_bool("is_honest_control", r.is_honest_control);
            w.field_bool("passed_verification", r.passed_verification);
            w.field_string("rejection_reason", r.rejection_reason.value_or(""));
            w.end_object();
        }
        w.end_array();
        w.end_object();
        std::cout << w.str() << "\n";
        return report.all_invariants_held ? 0 : 1;
    }

    std::cout << "============================================================\n"
              << " FairCross Adversarial Operator Attack Matrix Report\n"
              << "============================================================\n"
              << "Evaluated Fixture:       " << path << "\n"
              << "Total Vectors Evaluated: " << report.total_evaluated << "\n"
              << "Attacks Rejected:        " << report.total_attacks_rejected << "\n"
              << "All Invariants Held:     " << (report.all_invariants_held ? "YES" : "NO") << "\n"
              << "------------------------------------------------------------\n";
    for (const auto& r : report.results) {
        const bool expected = r.is_honest_control ? r.passed_verification : !r.passed_verification;
        std::cout << (r.is_honest_control ? "[CONTROL] " : (expected ? "[REJECT]  " : "[BREACH]  "))
                  << r.attack_name << "\n    " << r.description << "\n";
        if (r.rejection_reason.has_value()) {
            std::cout << "    Reason: " << r.rejection_reason.value() << "\n";
        }
    }
    return report.all_invariants_held ? 0 : 1;
}

int cmd_prove(const std::string& path, bool verify, bool json_only) {
    BatchFixture fixture = load_batch_fixture(path);
    blind_ledger(fixture.pre_state, kDemoVenueSecret);
    const auto preimages = derive_preimages(fixture.batch, 0);

    auto exec_res = execute_batch(fixture.pre_state, fixture.batch);
    if (exec_res.is_err()) {
        std::cerr << "Batch execution error: " << exec_res.error_message() << "\n";
        return 1;
    }
    const BatchExecutionResult& exec = exec_res.value();

    auto [merkle_tree, accounting] =
        build_complete_input_accounting(fixture.batch, preimages, exec.allocation);

    const InstrumentId inst = fixture.batch.instrument();
    BatchHeader header(1, fixture.batch.batch_id(), inst, 1700000000ULL,
                       static_cast<uint32_t>(fixture.batch.len()), merkle_tree.root());

    BatchProofPublicInputs public_inputs{
        compute_ledger_root(fixture.pre_state, inst),
        compute_ledger_root(exec.post_state, inst),
        compute_batch_commitment(header),
        compute_oracle_snapshot_commitment(std::nullopt, std::nullopt),
        exec.clearing_outcome.clearing_price.has_value()
            ? exec.clearing_outcome.clearing_price->as_raw()
            : 0,
        exec.clearing_outcome.executable_volume.as_raw()};

    BatchProofWitness witness{fixture.pre_state, exec.post_state, fixture.batch, preimages,
                              exec.allocation,   exec.fills,      accounting,
                              std::nullopt,      std::nullopt,    1700000000ULL};

    auto proof_res = SingleBatchProver::prove(public_inputs, witness);
    if (proof_res.is_err()) {
        std::cerr << "Proof generation error: " << proof_res.error_message() << "\n";
        return 1;
    }
    const BatchProof& proof = proof_res.value();

    if (verify) {
        auto v = SingleBatchVerifier::verify(public_inputs, proof);
        if (v.is_err()) {
            std::cerr << "Proof verification error: " << v.error_message() << "\n";
            return 1;
        }
    }

    const std::string certificate(proof.proof_bytes.begin(), proof.proof_bytes.end());

    if (json_only) {
        json::Writer w;
        write_batch_proof(w, public_inputs, proof);
        std::cout << w.str() << "\n";
        return 0;
    }

    std::cout << "============================================================\n"
              << " FairCross Single-Batch Transition Proof\n"
              << "============================================================\n"
              << "Pre-State Root:   " << public_inputs.pre_state_root.to_hex() << "\n"
              << "Post-State Root:  " << public_inputs.post_state_root.to_hex() << "\n"
              << "Batch Header:     " << public_inputs.batch_header_hash.to_hex() << "\n"
              << "Certificate:      " << certificate << "\n";
    if (verify) std::cout << "Verification:     PASSED [SAT]\n";
    return 0;
}

int cmd_prove_session(const std::string& path, bool verify, bool json_only) {
    SessionManifest manifest = load_session_manifest(path);
    blind_ledger(manifest.genesis_ledger, kDemoVenueSecret);
    if (manifest.batches.empty()) {
        std::cerr << "Session manifest contains zero batches\n";
        return 1;
    }

    const InstrumentId inst = manifest.batches[0].instrument();
    Ledger ledger = manifest.genesis_ledger;
    const Commitment genesis_root = compute_ledger_root(ledger, inst);
    const RunningState initial_state = RunningState::genesis(genesis_root, inst);

    Commitment current_root = genesis_root;
    std::vector<RecursiveStep> steps;
    steps.reserve(manifest.batches.size());

    for (size_t idx = 0; idx < manifest.batches.size(); ++idx) {
        const Batch& batch = manifest.batches[idx];
        const auto preimages = derive_preimages(batch, idx);

        auto exec_res = execute_batch(ledger, batch);
        if (exec_res.is_err()) {
            std::cerr << "Batch " << (idx + 1) << " execution error: " << exec_res.error_message()
                      << "\n";
            return 1;
        }
        const BatchExecutionResult& exec = exec_res.value();

        auto [merkle_tree, accounting] =
            build_complete_input_accounting(batch, preimages, exec.allocation);

        const uint64_t timestamp = 1700000000ULL + static_cast<uint64_t>(idx) * 10;
        BatchHeader header(1, batch.batch_id(), batch.instrument(), timestamp,
                           static_cast<uint32_t>(batch.len()), merkle_tree.root());

        const Commitment pre_root = current_root;
        const Commitment post_root = compute_ledger_root(exec.post_state, batch.instrument());
        current_root = post_root;

        BatchProofPublicInputs public_inputs{
            pre_root,
            post_root,
            compute_batch_commitment(header),
            compute_oracle_snapshot_commitment(std::nullopt, std::nullopt),
            exec.clearing_outcome.clearing_price.has_value()
                ? exec.clearing_outcome.clearing_price->as_raw()
                : 0,
            exec.clearing_outcome.executable_volume.as_raw()};

        BatchProofWitness witness{ledger,          exec.post_state, batch, preimages,
                                  exec.allocation, exec.fills,      accounting,
                                  std::nullopt,    std::nullopt,    timestamp};

        steps.push_back(RecursiveStep{public_inputs, witness, timestamp});
        ledger = exec.post_state;
    }

    auto session_res = RecursiveSessionProver::prove_session(initial_state, steps);
    if (session_res.is_err()) {
        std::cerr << "Session prove error: " << session_res.error_message() << "\n";
        return 1;
    }
    const SessionProof& session = session_res.value();

    if (verify) {
        auto v = RecursiveSessionVerifier::verify_session(session.public_inputs, session);
        if (v.is_err()) {
            std::cerr << "Session verify error: " << v.error_message() << "\n";
            return 1;
        }
    }

    const std::string certificate(session.proof_certificate.begin(),
                                  session.proof_certificate.end());

    if (json_only) {
        json::Writer w;
        w.begin_object();
        w.field_u64("session_version", session.session_version);
        w.field_u64("num_batches", session.num_batches);
        w.key("public_inputs");
        w.begin_object();
        w.key("initial_state");
        write_running_state(w, session.public_inputs.initial_state);
        w.key("final_state");
        write_running_state(w, session.public_inputs.final_state);
        w.end_object();
        w.key("step_proofs");
        w.begin_array();
        for (const BatchProof& step : session.step_proofs) {
            write_batch_proof(w, step.public_inputs, step);
        }
        w.end_array();
        field_byte_string(w, "proof_certificate", session.proof_certificate);
        w.end_object();
        std::cout << w.str() << "\n";
        return 0;
    }

    std::cout << "============================================================\n"
              << " FairCross Multi-Batch Recursive Session Proof\n"
              << "============================================================\n"
              << "Batches Folded:      " << session.num_batches << "\n"
              << "Final Batch ID:      " << session.public_inputs.final_state.batch_id.as_raw()
              << "\n"
              << "Final History Acc:   "
              << session.public_inputs.final_state.history_accumulator.to_hex() << "\n"
              << "Final Ledger Root:   " << session.public_inputs.final_state.ledger_root.to_hex()
              << "\n"
              << "Session Certificate: " << certificate << "\n";
    if (verify) std::cout << "Verification Check:  PASSED [SAT]\n";
    return 0;
}

int cmd_simulate(bool json_only) {
    const SyntheticFlowConfig config{};
    const SyntheticSession session = SyntheticOrderFlowSimulator::generate_session(config);

    if (json_only) {
        json::Writer w;
        w.begin_object();
        w.field_u64("seed", config.seed);
        w.field_u64("num_batches", session.batches.size());
        w.key("batches");
        w.begin_array();
        for (const auto& bundle : session.batches) {
            w.begin_object();
            w.field_u64("batch_id", bundle.batch.batch_id().as_raw());
            w.field_u64("cutoff_nanos", bundle.cutoff_nanos);
            w.field_u64("order_count", bundle.batch.len());
            w.end_object();
        }
        w.end_array();
        w.end_object();
        std::cout << w.str() << "\n";
        return 0;
    }

    std::cout << "============================================================\n"
              << " FairCross Synthetic Order-Flow Simulation\n"
              << "============================================================\n"
              << "NOTE: stylized synthetic flow for invariant and benchmark\n"
              << "      workloads only; not a behavioural market model.\n"
              << "Seed:            " << config.seed << "\n"
              << "Accounts:        " << config.num_accounts << "\n"
              << "Batches:         " << session.batches.size() << "\n"
              << "------------------------------------------------------------\n";
    for (const auto& bundle : session.batches) {
        std::cout << "Batch " << bundle.batch.batch_id().as_raw() << ": " << bundle.batch.len()
                  << " orders, cutoff " << bundle.cutoff_nanos << "\n";
    }
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const std::string cmd = argv[1];
    const bool json_only = has_flag(argc, argv, "--json");
    const bool verify = has_flag(argc, argv, "--verify");

    auto require_path = [&](const char* what) -> std::string {
        if (argc < 3 || argv[2][0] == '-') {
            std::cerr << "Missing required " << what << " argument\n";
            print_usage(argv[0]);
            std::exit(1);
        }
        return std::string(argv[2]);
    };

    try {
        if (cmd == "run") {
            return cmd_run(require_path("<fixture.json>"), json_only);
        }
        if (cmd == "attack-matrix") {
            return cmd_attack_matrix(require_path("<fixture.json>"), json_only);
        }
        if (cmd == "prove") {
            return cmd_prove(require_path("<fixture.json>"), verify, json_only);
        }
        if (cmd == "prove-session") {
            return cmd_prove_session(require_path("<manifest.json>"), verify, json_only);
        }
        if (cmd == "simulate") {
            return cmd_simulate(json_only);
        }
        if (cmd == "help" || cmd == "-h" || cmd == "--help") {
            print_usage(argv[0]);
            return 0;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    std::cerr << "Unknown subcommand: " << cmd << "\n";
    print_usage(argv[0]);
    return 1;
}
