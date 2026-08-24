# Transparent R1CS Cost Model Analysis

## 1. Scope and provenance

This report summarizes the measured work performed by FairCross's in-tree R1CS constraint
synthesis and certificate path. It does not measure zero-knowledge proving, SNARK/STARK polynomial
operations, multi-scalar multiplication, recursive IVC, or a production verifier.

The authoritative artifact is `experiments/results/proof_cost_model_report.json`, produced by
`cpp/bin/faircross_bench proof-cost-model` on the recorded Apple Silicon (`aarch64-apple-darwin`)
build. Its single-batch baseline is 37 constraints and 16 microseconds of transparent prove work.
The session rows combine that measured baseline with the recorded batch-interval shapes.

## 2. Complete measured table

| Batch interval (ms) | Mean batches | Mean batch size (orders) | Mean cleared volume (lots) | Single-batch work ($\mu$s) | Single-batch constraints | Total modeled session work (ms) | Modeled duty cycle (%) |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 10 | 232.667 | 1.182 | 2.000 | 16 | 37 | 3.723 | 0.037 |
| 50 | 149.000 | 1.846 | 2.333 | 16 | 37 | 2.384 | 0.024 |
| 100 | 94.333 | 2.915 | 4.000 | 16 | 37 | 1.509 | 0.015 |
| 250 | 39.667 | 6.933 | 0.000 | 16 | 37 | 0.635 | 0.006 |
| 500 | 20.000 | 13.750 | 0.000 | 16 | 37 | 0.320 | 0.003 |

The table is transcribed from the raw JSON fields. `single_batch_prove_time_us` means the
transparent in-tree constraint/certificate path; it is not a cryptographic proving time. The
session values are the artifact's modeled totals, not a claim that a deployment can meet a market
latency target.

## 3. Findings and limitations

The artifact establishes a reproducible baseline for the current implementation: the measured
single-batch path has 37 constraints and 16 microseconds of work in the recorded build, and the
modeled session rows range from 0.003% to 0.037% duty cycle. These values may help size future
engineering experiments, but they cannot establish a SNARK/IVC cost, zero-knowledge privacy,
succinctness, soundness, or production throughput.

In particular, the repository contains no measured polynomial division or MSM backend. Any future
cryptographic cost claim must identify its proving system, implementation, hardware, witness/public
inputs, and reproduction artifact separately from this transparent R1CS report.
