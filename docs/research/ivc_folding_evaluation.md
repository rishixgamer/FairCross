# History Accumulation and Future IVC Evaluation

## Status correction

This note began as a proof-backend design spike. FairCross v0 did **not** adopt or implement Nova,
CycleFold, a recursive SNARK, or another incrementally verifiable computation (IVC) backend. The
current C++20 implementation is a native SHA-256 running-state accumulator whose verifier retains
and replays every transparent step certificate.

Consequently, the implemented component provides deterministic history binding, not zero knowledge,
succinct proof soundness, constant-time verification, or a constant-size proof of the full session.
The precise current contract is documented in
[`docs/RECURSIVE_STATE_TRANSITION.md`](../RECURSIVE_STATE_TRANSITION.md).

## Implemented baseline

For each accepted batch, the implementation hashes a canonical step record containing the step
index, previous and next ledger roots, execution commitment, clearing price and quantity, and
allocation hash into a running state. A session verifier checks ledger-root continuity, replays each
single-batch transparent certificate, and rebuilds the chain.

The current comparison artifact records, at `K=50`:

- 2,800 bytes for independent per-batch artifact records;
- 122 bytes for the native accumulator certificate record;
- a 22.95x record-size ratio.

Those values describe serialized in-tree records. Verification remains linear in the retained step
certificates, and the 122-byte record is not a cryptographic proof. Reproduce the artifact with the
command embedded in
[`experiments/results/zk_recursive_vs_independent_comparison.json`](../../experiments/results/zk_recursive_vs_independent_comparison.json).

## Candidate architectures for future work

The original spike considered three families:

1. recursive verification of a SNARK inside another circuit;
2. Nova-style relaxed-R1CS folding;
3. a native hash-chain accumulator around transparent relation checks.

Only the third exists in this repository, and the term "IVC" is deliberately not used for it. The
first two remain unimplemented candidates. Earlier latency, constraint-count, MSM, and verifier-cost
estimates in this note were not produced by the current C++ system and are withdrawn as evidence.

## Decision gate for a real proving backend

No proving backend is selected. A future adoption requires a new ADR that pins:

- the concrete library, version, curve/field, and security assumptions;
- the exact public statement and private witness;
- commitment/hash compatibility and any translation gadgets;
- soundness, completeness, privacy, and recursion tests;
- reproducible prover/verifier latency, memory, and proof-size measurements;
- supply-chain and native C++ integration costs.

Until that gate is met, FairCross must describe the shipped system as a transparent constraint
harness plus native history accumulator.
