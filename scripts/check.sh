#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

echo "== FairCross C++20 Verification Gate =="

# The golden vectors are frozen reference values, not regenerated per run. This
# confirms they are present, parseable, and provenance-stamped before the suite
# asserts against them: an unstamped or missing vector file would otherwise turn
# a parity assertion into a silent skip.
echo "-- Verifying frozen golden vectors --"
python3 scripts/verify_golden_vectors.py

cd "$ROOT/cpp"

echo "-- Building FairCross C++ with ASan + UBSan --"
make clean
make -j4

echo "-- Running C++ Test Suite --"
# Property tests run a fixed default seed so the gate is reproducible run to run.
# To widen a sweep or replay a reported counterexample:
#   FAIRCROSS_PROPERTY_CASES=10000 ./bin/faircross_tests
#   FAIRCROSS_PROPERTY_SEED=<seed from the failure message> ./bin/faircross_tests
./bin/faircross_tests

echo "-- Verifying CLI Subcommands --"
# Every advertised subcommand is exercised, so usage text and implementation
# cannot drift apart.
cd "$ROOT"
CPP=cpp/bin/faircross_cpp
"$CPP" help >/dev/null
"$CPP" run fixtures/golden/01_normal_clearing.json --json >/dev/null
"$CPP" attack-matrix fixtures/sample_batch.json --json >/dev/null
"$CPP" prove fixtures/golden/01_normal_clearing.json --verify --json >/dev/null
"$CPP" prove-session fixtures/sample_session.json --verify --json >/dev/null
"$CPP" simulate --json >/dev/null

echo "PASS"
