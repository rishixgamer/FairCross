# Plaintext Engine Performance Profile Report

## 1. Environment and methodology

The authoritative artifact is `experiments/results/plaintext_engine_profile.json`, produced by
`cpp/bin/faircross_bench engine-profile` with 100 iterations per point.

- **Environment**: macOS, aarch64
- **Compiler**: clang++ 21.0.0, C++20, `-O2`, no sanitizers
- **Workload**: the recorded deterministic benchmark workload in the C++ harness

These are controlled prototype measurements, not live-market latency or throughput guarantees.

## 2. Latency and throughput scaling

| Batch size ($N$) | Mean execution ($\mu$s) | p50 ($\mu$s) | p90 ($\mu$s) | p99 ($\mu$s) | Mean verification ($\mu$s) | Throughput (orders/s) |
|---:|---:|---:|---:|---:|---:|---:|
| 10 | 1.100 | 1 | 1 | 4 | 1.020 | 9,090,909.091 |
| 50 | 6.110 | 6 | 6 | 9 | 6.140 | 8,183,306.056 |
| 100 | 12.140 | 12 | 12 | 16 | 13.260 | 8,237,232.290 |
| 250 | 32.610 | 32 | 34 | 47 | 37.610 | 7,666,360.012 |
| 500 | 79.720 | 68 | 77 | 420 | 85.130 | 6,271,951.831 |

The raw artifact reports no instrumented function-level hotspot breakdown, so this report makes no
allocation/sorting percentage claim. Measurements are comparable only to runs using the same
workload, build, and environment metadata.
