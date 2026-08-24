#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

echo "============================================================"
echo "FairCross Master Reproducibility Experiment Runner"
echo "============================================================"

# The verification gate builds the sanitized test binary and runs the full
# suite. It is run first so that no measurement is recorded from a tree that
# does not pass its own invariant checks.
echo "[1/2] Running verification gate..."
./scripts/check.sh

# The benchmark binary is built -O2 with no sanitizers, for the reasons given in
# cpp/bench/harness.hpp. Each benchmark writes its artifact under
# experiments/results/ with the environment metadata and reproduction command
# that produced it.
echo "[2/2] Running all benchmarks..."
cd "$ROOT/cpp"
make -j4 bin/faircross_bench
cd "$ROOT"
cpp/bin/faircross_bench all

echo "============================================================"
echo "All experiments completed successfully."
echo "Raw outputs recorded in experiments/results/"
echo "============================================================"
