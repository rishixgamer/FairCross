# Research Paper Outline: Procedural Fairness in Frequent-Batch Exchanges

**Working Title**: *FairCross: Procedural Fairness and Verifiable State Transitions in Frequent-Batch Exchanges*

**Research question**:
Can a deterministic integer-arithmetic batch exchange make its execution procedure independently
auditable through complete-input accounting, explicit state invariants, and a transparent in-tree
R1CS research harness? The current repository does not establish zero-knowledge privacy, succinct
proof soundness, recursive IVC, or production proving performance; those remain future research.

## Provenance and evidence boundary

Every measurement cited in this outline is reproducible from the command embedded in its raw JSON
artifact under `experiments/results/`. Timing artifacts use an `-O2` unsanitized benchmark build;
the test suite uses ASan/UBSan. Engine semantics, constraint counts, and history folding are pinned
by frozen golden vectors, which detect drift from recorded values rather than providing a second
implementation. Raw artifacts are authoritative over earlier prose summaries.

## Section 1: Introduction and motivation

1. Frequent-batch execution can reduce speed-race incentives, but an opaque operator can still omit
   orders, alter deterministic tie breaks, overfill, or create balances.
2. Procedural fairness is stated as explicit invariants: complete committed-input accounting,
   deterministic clearing and allocation, fill bounds, and conservation of cash and inventory.
3. The contribution is an inspectable C++20 research artifact whose plaintext engine, commitments,
   transparent R1CS checks, and native history accumulator can be measured and falsified.

## Section 2: System architecture and threat model

1. Traders submit salted commitments; the operator reveals/uses plaintext order data during clearing.
2. The engine performs deterministic single-instrument uniform-price clearing and atomic settlement.
3. SHA-256 commitments, Merkle accounting, ledger roots, and oracle freshness/collar checks bind the
   published transition statements.
4. The R1CS component checks selected relations in process. The native history accumulator hashes
   successive public step data. Neither component hides witnesses from the operator or supplies a
   remote cryptographic proof boundary.
5. Out of scope: operator-blind order privacy, network ingress censorship before commitment-root
   inclusion, authenticated external oracle publishers, multi-asset margin, and production network
   or custody controls.

## Section 3: Mechanism specification

1. Prices, quantities, and cash use integer/fixed-point domain types.
2. Clearing maximizes executable volume over candidate prices with deterministic midpoint/tie rules.
3. Hare–Niemeyer largest-remainder allocation distributes integer lots using deterministic ordering.
4. Every committed leaf receives a disposition; account-level settlement reconstruction enforces
   conservation and fill/limit-price invariants.

## Section 4: Transparent verification harness

1. `SingleBatchProver` first runs the plaintext transition/accounting checks, synthesizes documented
   R1CS subrelations, checks satisfiability, and emits a readable local certificate.
2. `RecursiveSessionProver` builds a native hash-chained running state. `RecursiveSessionVerifier`
   replays each step proof, checks ledger-root continuity, and rebuilds the accumulator.
3. This section must not call either artifact a ZK proof, SNARK/STARK, recursive IVC, or succinct
   proof. A production backend is a research question requiring a selected proving system and new
   security analysis.

## Section 5: Experimental evaluation

1. **Adversarial matrix (EXP-002)**: the raw artifact evaluates 12 vectors: one honest control and
   11 malicious mutations. All 11 malicious cases are rejected and the honest control passes.
2. **Transparent proof scaling (EXP-004)**: for batch sizes 2–64, measured R1CS constraints range
   from 37 to 793; recorded prove times range from 50 to 991 microseconds. The proof-size field is
   a readable in-tree certificate, not a cryptographic proof size.
3. **Native history comparison (EXP-007)**: at $K=50$, independent artifacts total 2,800 bytes and
   the native accumulator certificate is 122 bytes, a 22.95x storage ratio. The verifier still
   replays step artifacts, so this does not support constant verification or recursive soundness.
4. **Transparent proof-cost model (EXP-009)**: the single-batch baseline is 16 microseconds for 37
   constraints. Recorded 10–500 ms rows report 0.320–3.723 ms total modeled work and 0.003%–0.037%
   modeled duty cycle. No SNARK polynomial/MSM cost is present in the artifact.
5. **Plaintext engine profile (EXP-011)**: the current five-point table reports mean execution of
   1.100–79.720 microseconds for batch sizes 10–500 and the corresponding verification/throughput
   columns in `plaintext_engine_profile.json`.
6. **Interval matrix (EXP-008)**: the current raw JSON contains 15 runs over three seeds and reports
   batch counts, fills, submitted volume, and cleared volume for 10, 50, 100, 250, and 500 ms.
   It does not support a stable-fill-rate or execution-delay claim by itself.

## Section 6: Limitations and future work

1. Implement and benchmark a specified production proving backend, with independent soundness and
   privacy analysis, if a cryptographic proof claim is desired.
2. Study commitment/preimage privacy, ingress receipts, authenticated multi-source oracles, and
   side-channel/network leakage under an expanded threat model.
3. Extend the single-instrument spot model only with a recorded decision and invariant coverage.
4. Keep modeled prototype measurements separate from live-market or deployment claims.

## Section 7: Reproducibility artifacts

- `experiments/MANIFEST.json` and `./scripts/reproduce_all_experiments.sh` describe the artifact set.
- `docs/LIMITATIONS.md` records the verification-strength caveat and negative results.
- Frozen golden vectors pin semantics and measured structural counts; they are not independent
  implementations.
