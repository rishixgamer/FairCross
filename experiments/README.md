# FairCross Empirical Research & Reproducibility Guide

## 1. Overview

This directory contains the machine-readable experiment manifest (`MANIFEST.json`) and the raw empirical result datasets for the **FairCross** privacy-preserving, cryptographically auditable frequent-batch exchange.

All empirical claims are reproducible from a clean checkout without manual notebook steps or undocumented tuning.

---

## 2. Environment & Prerequisites

- **Language & Toolchain**: C++20. Tested with `clang++` 21; `clang++ >= 15` or `g++ >= 12` are expected to work.
- **Build system**: `make` (see `cpp/Makefile`). No third-party libraries are required.
- **Operating Systems**: macOS (`aarch64-apple-darwin`), Linux (`x86_64-linux-gnu`, `aarch64-linux-gnu`).
- **Hardware Requirements**: Any standard commodity CPU (Apple Silicon M1/M2/M3/M4, Intel Core i7/i9, AMD Ryzen) with $\ge 4\text{ GB}$ RAM.
- **Hardware Limitations**: High-throughput zero-copy Linux AF_XDP is documented and evaluated in `docs/research/linux_af_xdp_evaluation.md` as deferred on non-Linux hosts; all core experiments execute portably on macOS and Linux.

### Sanitizers and timing

The test binary builds with ASan and UBSan; the benchmark binary does not. The sanitizers inflate timings by a large and variable factor, so any number measured under them would be neither reproducible nor comparable. Correctness is always checked sanitized; timing is always measured unsanitized. Do not merge the two builds.

---

## 3. Quickstart: Reproduce All Experiments

To execute the verification gate and the entire benchmark suite in sequence:

```bash
./scripts/reproduce_all_experiments.sh
```

To run only the verification gate:

```bash
./scripts/check.sh
```

To run a single benchmark, build the benchmark binary and name the experiment:

```bash
cd cpp && make -j4 bin/faircross_bench && cd .. && cpp/bin/faircross_bench proof-scaling
```

---

## 4. Experiment Inventory

| ID | Name | Execution Command | Output Artifacts |
|---|---|---|---|
| **EXP-001** | Verification Gate & Invariant Tests | `./scripts/check.sh` | Terminal Output (`PASS`) |
| **EXP-002** | Adversarial Operator Attack Matrix | `cpp/bin/faircross_bench attack-evidence` | `attack_evidence_table.json`, `.csv` |
| **EXP-003** | ZK Accounting Constraint Benchmark | `cpp/bin/faircross_bench accounting-metrics` | `zk_accounting_metrics.json` |
| **EXP-004** | Single-Batch ZK Proof Scaling ($N \in [2, 64]$) | `cpp/bin/faircross_bench proof-scaling` | `zk_proof_scaling_benchmark.json` |
| **EXP-005** | 2-Step IVC Folding Benchmark Spike | `cpp/bin/faircross_bench two-step-ivc` | `zk_two_step_ivc_spike.json` |
| **EXP-006** | 10-Batch Running Audit Certificate | `cpp/bin/faircross_bench ten-batch-audit` | `zk_ten_batch_audit_metrics.json` |
| **EXP-007** | Recursive Folding vs Independent Proofs | `cpp/bin/faircross_bench recursive-vs-independent` | `zk_recursive_vs_independent_comparison.json` |
| **EXP-008** | Multi-Interval Market Simulation Matrix | `cpp/bin/faircross_bench batch-interval-matrix` | `batch_interval_matrix_raw.json`, `.csv` |
| **EXP-009** | Integrated Proof Cost & Duty Cycle Model | `cpp/bin/faircross_bench proof-cost-model` | `proof_cost_model_report.json`, `.csv` |
| **EXP-010** | ITCH 5.0 Parsing & L3 Book Replay | `cpp/bin/faircross_tests` | Terminal Test Output |
| **EXP-011** | Plaintext Engine Performance Profiling | `cpp/bin/faircross_bench engine-profile` | `plaintext_engine_profile.json`, `.csv` |
| **EXP-012** | Clearing Optimality Constraint Metrics | `cpp/bin/faircross_bench clearing-optimality` | `zk_clearing_optimality_metrics.json` |
| **EXP-013** | Auction Optimization Comparison | `cpp/bin/faircross_bench auction-optimization` | `auction_optimization_comparison.json` |

All output artifacts are written under `experiments/results/`. Each carries its own `environment` block and `reproduction_command`, so a measurement stays attributable to the build that produced it.
