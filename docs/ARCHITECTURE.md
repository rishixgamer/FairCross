# Architecture

## Layering

```text
┌──────────────────────────────────────────────┐
│ experiments / CLI / adversarial harness      │
├──────────────────────────────────────────────┤
│ proof / recursion                            │
├──────────────────────────────────────────────┤
│ commitments / authenticated state            │
├──────────────────────────────────────────────┤
│ deterministic market transition              │
│ orders → batch → clear → allocate → balances │
├──────────────────────────────────────────────┤
│ domain primitives                            │
└──────────────────────────────────────────────┘
```

Optional later layer:

```text
ITCH / offline replay → market-data book → reference-price snapshots → FairCross
```

## Module layout

Headers live under `cpp/include/faircross/`, and sources under `cpp/src/`.

```text
cpp/
  include/faircross/
    domain/        # Price, Qty, Side, OrderId, AccountId, Money
    engine/        # validation, clearing, allocation, state transition
    commitments/   # hashes, Merkle commitments, inclusion/accounting
    adversary/     # intentionally malicious operators/transitions
    proof/         # R1CS constraint system, prover, recursive folding
    simulator/     # synthetic order flow and microstructure experiments
    marketdata/    # ITCH/offline replay + optional Linux AF_XDP work
    util/          # canonical JSON reader/writer
  src/             # implementations, mirroring the header tree
  src/cli/         # reproducible demos/commands
  tests/           # example, property, conformance, and golden-vector suites
  bench/           # benchmark harness writing experiments/results/
```

## Determinism contract

- Prices are integer ticks.
- Quantities are integer lots/units.
- Cash is integer smallest units or a documented fixed-point scale.
- Sorting keys are total orders, never unstable implementation details.
- Tie-breaking is explicit and tested.
- Serialization used for commitments is canonical and versioned.
- Re-running a transition with identical state/input must produce byte-identical logical output.

## Proof architecture

The repository implements a transparent R1CS research model, not a production proving backend.
`SingleBatchProver` first runs the ordinary plaintext transition checker, synthesizes the documented
subrelations, and checks satisfaction in process. Its emitted certificate is a compact, readable
record of that successful local check. `SingleBatchVerifier` is therefore a harness verifier, not a
cryptographic SNARK verifier and not a security boundary.

The recursive layer is likewise a native hash-chained running-state accumulator. It demonstrates
deterministic history binding and the interface a future recursive system would consume; it does not
provide a Nova, Halo2, Groth16, or other succinct proof.

The two layers are kept apart so that a future production backend has a fixed target: it must prove
the same semantic relation the plaintext engine already tests, pin its material cryptographic
dependencies, and carry its own soundness analysis. Writing the market rules directly into a circuit
instead would produce a second, subtly different exchange implementation.
