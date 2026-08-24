# FairCross limitations and evidence boundaries

FairCross is a C++20 research prototype for deterministic, auditable frequent-batch execution. This
document separates what the artifact checks from what it does not prove.

## 1. What the prototype checks

For valid single-instrument batches, the implementation checks:

- deterministic maximum-volume clearing and the published midpoint tie-break;
- deterministic price-priority and largest-remainder allocation;
- fill bounds and limit-price compliance;
- exact cash and inventory conservation, including per-account post-state reconstruction;
- one canonical disposition for every committed batch leaf;
- binding of the batch header, canonical order commitments, ledger roots, and optional oracle
  statement;
- oracle freshness and price-collar policy; and
- continuity and ordering of the native running-state accumulator.

These properties are exercised by unit, property, conformance, adversarial, and frozen-vector tests.

## 2. Privacy and operator trust

Orders are plaintext to the venue operator before cutoff. Salted commitments hide order data from
observers who do not have the preimage, but FairCross does not implement threshold encryption,
secure multiparty computation, time-lock encryption, or operator-blind matching.

Ledger leaves use persistent per-account salts derived from a venue secret. This prevents simple
balance enumeration by observers, but it does not hide balances from the operator, conceal the
account set, or provide per-batch unlinkability. Reuse of a salt reveals whether a leaf changed
between roots.

## 3. Ingress censorship

Complete-input accounting begins at the published batch commitment. It detects omission or
misclassification of a committed leaf, but it cannot detect a packet dropped before the order enters
that commitment. Signed ingress receipts or distributed sequencing would be required to address
that boundary.

## 4. Oracle boundary

The committed oracle statement binds one reference snapshot and the freshness/collar policy used to
validate it. FairCross does not authenticate a real publisher, aggregate multiple venues, or protect
against manipulation of the source market itself.

## 5. Market and operational scope

The core mechanism is single-instrument, long-only spot execution. Authentication, custody, margin,
fees, cross-asset risk, networking, regulatory controls, high availability, and live order routing
are outside scope. The bounded ITCH parser is an offline research subset, not a production feed
handler.

## 6. Transparent proof-harness boundary

`SingleBatchProver` executes the plaintext checks, synthesizes selected R1CS relations, verifies
satisfaction in process, and emits a readable certificate. That certificate does not contain a
cryptographic proof of witness possession. `SingleBatchVerifier` checks public-input consistency and
certificate grammar but cannot independently establish satisfaction against a malicious prover.

The running-state accumulator hashes ordered step statements and requires ledger-root continuity.
Its verifier retains and replays the individual step certificates. It is not a zero-knowledge,
succinct, recursive, or incrementally verifiable cryptographic proof, and it does not provide
constant-time external verification.

## 7. Verification-strength boundary

Different checks provide different evidence:

| Surface | Evidence | Important limit |
|---|---|---|
| Canonical encodings and Merkle rules | Spec-derived C++ conformance reference plus randomized and frozen vectors | Reference and production share the in-tree SHA-256 primitive and author |
| Market semantics | Unit/property tests and frozen vectors | Frozen vectors detect recorded drift; they are not an independent implementation |
| R1CS structure and history accumulator | Unit/adversarial tests and frozen structural values | No external proving backend or independent semantic implementation |
| CLI | Every advertised subcommand exercised by the gate | Smoke coverage is not independent validation of every output value |
| Experiments | Raw artifacts with environment metadata and reproduction commands | Controlled prototype results do not establish live-market or production performance |

Re-freezing a vector is a semantic decision, not a routine way to make a failing test pass.

## 8. Empirical-claim boundary

Timing results apply only to the compiler, hardware, workload, and build configuration recorded in
each artifact. Transparent R1CS synthesis time is not cryptographic proving time. Simulator output is
not evidence of alpha, stable fill quality, or deployment throughput. Modeled values are labeled as
models and must not be presented as measurements.

Closing any of these boundaries requires new implementation evidence, threat-model analysis, and
reproducible artifacts. Rewording the claim does not close it.
