# FairCross

[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C.svg)](cpp/)
[![verification](https://github.com/rishixgamer/FairCross/actions/workflows/ci.yml/badge.svg)](https://github.com/rishixgamer/FairCross/actions/workflows/ci.yml)
[![dependencies](https://img.shields.io/badge/third--party_dependencies-0-2ea44f.svg)](docs/DECISIONS.md)
[![license](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

**A C++20 research prototype for deterministic, auditable frequent-batch exchange execution.**

FairCross asks a narrow market-structure question: can a private venue make its execution procedure
independently checkable without publishing every order? The repository implements the plaintext
state machine first, then layers canonical commitments, invariant checks, a transparent R1CS
research model, adversarial tests, and reproducible experiments around it.

> FairCross is an engineering and research artifact. It is not a broker, a live trading system, a
> production exchange, or a production zero-knowledge prover.

## Engineering snapshot

| Area | What is implemented |
|---|---|
| **Market semantics** | Single-instrument uniform-price batch auction; integer ticks, lots, and 128-bit cash; deterministic midpoint price selection and Hare–Niemeyer largest-remainder allocation |
| **State integrity** | Per-account settlement reconstruction, cash/inventory conservation, fill bounds, limit-price checks, and oracle freshness/collar validation |
| **Cryptographic accounting** | Domain-separated SHA-256 commitments, Merkle inclusion, salted ledger roots, canonical byte encodings, and an ADR-derived conformance implementation |
| **Verification research** | In-tree R1CS synthesis and satisfiability checks plus a hash-chained running-state accumulator; no production SNARK/IVC backend is claimed |
| **Market data & experiments** | Deterministic simulator, a bounded ITCH 5.0 message subset with Level-3 replay, adversarial mutation matrix, and machine-readable benchmark artifacts |
| **Implementation** | C++20, no third-party libraries, strict warnings, ASan/UBSan, fixed-seed property tests, and frozen golden vectors |

C++20 under [`cpp/`](cpp/) is the sole source of market semantics.

## Verification you can reproduce

```bash
git clone https://github.com/rishixgamer/FairCross.git
cd FairCross
./scripts/check.sh
```

The gate verifies seven provenance-stamped frozen-vector files, builds with strict warnings plus
AddressSanitizer and UndefinedBehaviorSanitizer, runs the unit/property/conformance suite, and
exercises every advertised CLI subcommand. To widen the deterministic property sweep:

```bash
FAIRCROSS_PROPERTY_CASES=10000 ./cpp/bin/faircross_tests
```

To regenerate every current experiment artifact:

```bash
./scripts/reproduce_all_experiments.sh
```

Each file in [`experiments/results/`](experiments/results/) carries its own environment block and
reproduction command. Where a number in this README disagrees with the artifact it came from, the
artifact is correct.

### Current evidence snapshot

- The adversarial matrix contains one honest control and 11 malicious operator mutations; all 11
  malicious cases are rejected in the recorded artifact.
- Commitment conformance is derived independently from the ADR byte layouts and exercised across
  randomized inputs. The two implementations still share the in-tree SHA-256 primitive and author,
  so this is stronger than a round-trip test but weaker than independent teams and libraries.
- Proof-scaling artifacts replay the real in-tree constraint synthesis: the recorded batch-size
  sweep from 2 to 64 orders measures 37 to 793 R1CS constraints.
- Market semantics, constraint counts, and recursive folding are additionally pinned by frozen
  golden vectors. Those vectors detect drift; they are not a second implementation.

See the [`experiment manifest`](experiments/MANIFEST.json),
[`limitations memo`](docs/LIMITATIONS.md), and
[`proof statement`](docs/PROOF_STATEMENT.md) for the precise evidence boundary.

## Architecture

```text
experiments / CLI / adversarial harness
                    │
transparent R1CS model / running-state accumulator
                    │
commitments / authenticated state / oracle policy
                    │
orders → clear → allocate → canonical fills → balances
                    │
integer domain primitives and canonical encodings
```

The boundaries matter for a practical reason. Market rules are tested as ordinary deterministic C++
before they are represented as constraints, and cryptographic encoding is kept separate from
allocation semantics, so a bug in one layer does not hide in the other.

## Repository map

| Path | Start here for |
|---|---|
| [`cpp/include/faircross/`](cpp/include/faircross/) | Public domain, engine, commitment, proof, simulator, and market-data interfaces |
| [`cpp/src/engine/`](cpp/src/engine/) | Clearing, deterministic allocation, fills, settlement, and invariant checking |
| [`cpp/tests/`](cpp/tests/) | Unit, property, conformance, adversarial, and golden-vector tests |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | System layers and determinism contract |
| [`docs/DECISIONS.md`](docs/DECISIONS.md) | Append-only architecture and research decision record |
| [`docs/THREAT_MODEL.md`](docs/THREAT_MODEL.md) | Adversaries, trust boundaries, and explicit non-claims |
| [`docs/DEVELOPMENT_PROCESS.md`](docs/DEVELOPMENT_PROCESS.md) | Review, verification, and contribution discipline |
| [`experiments/README.md`](experiments/README.md) | Reproduction commands and artifact inventory |
| [`manual/index.html`](manual/index.html) | Interactive local walkthrough of the project concepts |

## Scope and non-claims

- Orders are plaintext to the operator before cutoff; there is no MPC, threshold encryption, or
  operator-blind privacy.
- The in-tree proof certificate is a transparent research harness around R1CS satisfaction, not a
  succinct or zero-knowledge cryptographic proof and not a production security boundary.
- The core market is single-instrument spot execution. Authentication, custody, margin, networking,
  regulatory controls, and live order routing are out of scope.
- Simulator and timing artifacts are controlled prototype measurements on the environment recorded
  in each file. They are not evidence of live-market quality, alpha, or production throughput.

## License

FairCross is available under the [MIT License](LICENSE).
