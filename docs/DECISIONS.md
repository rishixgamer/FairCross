# FairCross design decisions

This document records the decisions that define the current C++20 research artifact. Each entry
separates the implemented choice from alternatives, assumptions, evidence, and the conditions that
would justify revisiting it.

## ADR-001: plaintext semantics before proof constraints

**Status:** accepted

**Decision:** Define the frequent-batch auction and ledger transition as deterministic plaintext
C++ functions before expressing any relation in R1CS.

**Alternatives considered:** define the mechanism directly inside a circuit; start from a production
proving framework; couple market rules to commitment encodings.

**Rationale:** A proof system can only attest to the relation it encodes. Keeping market semantics
independently executable makes allocation, conservation, and tie-breaking reviewable without
cryptographic machinery and prevents the circuit from silently inventing market rules.

**Assumptions:** the plaintext transition is the canonical specification for the single-instrument
spot market implemented here.

**Evidence:** `cpp/src/engine/`, `cpp/tests/test_auction.cpp`,
`cpp/tests/test_allocation.cpp`, and `cpp/tests/test_checker.cpp`.

**Reconsider when:** a formal specification becomes authoritative and is checked against both the
plaintext engine and proof relation.

## ADR-002: C++20 and a dependency-free core

**Status:** accepted

**Decision:** Implement FairCross in C++20 with no third-party build or runtime dependencies. Build
with strict conversion and shadowing warnings, AddressSanitizer, and UndefinedBehaviorSanitizer.

**Alternatives considered:** a managed runtime; multiple production implementations; external
libraries for JSON, property generation, hashing, and test infrastructure.

**Rationale:** The artifact depends on exact integer arithmetic and byte-exact canonical encodings.
A small self-contained tree keeps the audited surface visible and makes the reproduction gate easy
to run on a clean machine.

**Assumptions:** sanitizer coverage is limited to executed paths, and the in-tree utilities remain
small enough to review. Dependency count is not evidence of security by itself.

**Evidence:** `cpp/CMakeLists.txt`, `cpp/Makefile`, `.github/workflows/ci.yml`, and
`./scripts/check.sh`.

**Reconsider when:** a reviewed dependency materially reduces security risk or maintenance burden,
or a second implementation gains a committed independent owner.

## ADR-003: integer market state and explicit overflow handling

**Status:** accepted

**Decision:** Represent prices and quantities with unsigned 64-bit domain types and cash with an
unsigned 128-bit domain type. Active orders require positive price and quantity. Arithmetic that can
overflow, underflow, or narrow is checked before state mutation. Exchange state never uses floating
point.

**Alternatives considered:** decimal floating point; binary floating point; implicit wraparound;
restricting valid values solely to simplify intermediate arithmetic.

**Rationale:** Integer ticks and lots make conservation exact and deterministic across platforms.
Checked operations fail closed instead of turning malformed or extreme inputs into valid state.

**Assumptions:** the configured widths cover the prototype's single-instrument experiments; domain
validation is applied at every untrusted boundary.

**Evidence:** `cpp/include/faircross/domain/primitives.hpp`,
`cpp/src/domain/primitives.cpp`, and `cpp/tests/test_primitives.cpp`.

**Reconsider when:** multi-currency precision, fractional lots, or a wider settlement domain is
required.

## ADR-004: canonical batch intake and ordering

**Status:** accepted

**Decision:** A batch contains one instrument, rejects duplicate order identifiers and invalid
domain values, and canonicalizes orders by the documented sequence and identifier rules. File-backed
fixtures enter through validated factories.

**Alternatives considered:** preserve container insertion order; rely on caller validation; dedupe
silently; use nondeterministic hash iteration.

**Rationale:** Clearing, Merkle positions, accounting records, fills, and proof inputs must agree on
one order sequence. Rejecting malformed input before mutation keeps all later relations simpler.

**Assumptions:** a sequence value is assigned before the batch closes and is part of the committed
order semantics.

**Evidence:** `cpp/include/faircross/engine/batch.hpp`, `cpp/src/engine/batch.cpp`,
`cpp/tests/test_auction.cpp`, and CLI fixture tests in `scripts/check.sh`.

**Reconsider when:** multi-instrument or conditional orders require a different canonical key.

## ADR-005: maximum-volume uniform clearing with an overflow-safe midpoint

**Status:** accepted

**Decision:** Evaluate executable demand and supply over the sorted unique order prices, maximize
executed volume, and choose the floor midpoint of the lowest and highest maximizing prices. Compute
the midpoint as `low + (high - low) / 2`.

**Alternatives considered:** continuous price search; minimum or maximum maximizing price;
imbalance-first tie-breaking; arrival-order tie-breaking.

**Rationale:** The rule is deterministic, integer-only, permutation invariant where intended, and
easy to reproduce. The subtractive midpoint covers the full price domain without wraparound.

**Assumptions:** candidate order prices contain a maximum-volume solution for this discrete
single-instrument limit-order mechanism.

**Evidence:** `cpp/src/engine/auction.cpp`, `cpp/tests/test_auction.cpp`, property tests, and frozen
auction vectors under `fixtures/golden/expected/`.

**Reconsider when:** the venue adopts an imbalance, reference-price, or volatility-sensitive
tie-break.

## ADR-006: price priority and Hare–Niemeyer allocation

**Status:** accepted

**Decision:** Fill strictly better-priced orders before at-the-money orders. Allocate any marginal
short side proportionally in integer lots using largest remainders, with canonical order position as
the final deterministic tie-break.

**Alternatives considered:** time priority for the entire batch; random lottery; fractional fills;
round-robin allocation.

**Rationale:** The rule preserves price priority, avoids floating point, allocates exactly the
executable quantity, and publishes every tie-break needed to reproduce the result.

**Assumptions:** participants accept deterministic pro-rata allocation as the procedural-fairness
policy for simultaneous batch orders.

**Evidence:** `cpp/src/engine/allocation.cpp`, `cpp/tests/test_allocation.cpp`, permutation
properties, and the pro-rata golden fixture.

**Reconsider when:** empirical market-quality work supports a different published allocation rule.

## ADR-007: atomic settlement and independent transition checking

**Status:** accepted

**Decision:** Apply fills to a working ledger, reject insufficient balances or inventory, and commit
only after global cash and asset conservation hold. The verifier regenerates the canonical result,
reconstructs the expected post-state account by account, and compares every fill field and ledger
entry.

**Alternatives considered:** check aggregate totals only; trust caller-supplied fills; mutate the
ledger incrementally and roll back errors; duplicate settlement logic inside proof code.

**Rationale:** Aggregate conservation alone admits participant-level theft. Canonical fill equality
and per-account reconstruction bind the published transition to the actual auction result.

**Assumptions:** the prototype is long-only and single-instrument; self-trades that net to zero are
valid when they follow the canonical allocation.

**Evidence:** `cpp/src/engine/accounting.cpp`, `cpp/src/engine/checker.cpp`,
`cpp/tests/test_checker.cpp`, and the adversarial mutation suite.

**Reconsider when:** margin, shorting, fees, multiple assets, or legal novation changes settlement
semantics.

## ADR-008: deterministic property tests and frozen vectors

**Status:** accepted

**Decision:** Use an in-tree fixed-seed property harness with shrinking for market invariants. Pin
externally observable market, commitment, constraint-count, and session behavior in
provenance-stamped frozen vectors that are not regenerated during the normal gate.

**Alternatives considered:** example tests only; clock-seeded fuzzing; regenerate expected values on
every run; treat the production implementation as its own oracle.

**Rationale:** Fixed seeds make failures reproducible; properties widen state-space coverage; frozen
vectors detect semantic drift. These controls complement one another but do not constitute a second
independent implementation.

**Assumptions:** properties are non-vacuous and exercised against targeted mutations; vector changes
require an explicit semantic decision rather than an updated snapshot.

Provenance stamps name the release that publishes the vectors rather than a historical commit. This
keeps provenance stable across repository maintenance without changing any pinned semantic value.

**Evidence:** `cpp/tests/property.hpp`, `cpp/tests/test_properties.cpp`,
`cpp/tests/test_golden_vectors.cpp`, and `scripts/verify_golden_vectors.py`.

**Reconsider when:** an external conformance suite or formally generated oracle becomes available.

## ADR-009: canonical encodings and domain-separated SHA-256 commitments

**Status:** accepted

**Decision:** Encode commitment preimages with fixed-width, versioned, little-endian fields and
distinct domain tags. Commit orders, batch headers, oracle statements, and ledger leaves with the
in-tree SHA-256 implementation. Build Merkle trees with explicit leaf, node, and empty-node rules.

**Alternatives considered:** compiler struct layout; textual JSON; ambiguous concatenation; one hash
domain for every object; an unselected proof-oriented hash.

**Rationale:** A commitment is useful only when every byte and tree rule is unambiguous. Separating
market semantics from encoding lets reviewers reason about each layer independently.

**Assumptions:** SHA-256 collision resistance and binding are adequate for this research artifact;
the in-tree primitive receives less external scrutiny than a widely deployed library.

**Evidence:** `cpp/src/commitments/`, `docs/PROOF_STATEMENT.md`, NIST digest tests,
`cpp/tests/test_commitments.cpp`, and the spec-derived conformance reference.

**Reconsider when:** a production proof backend requires a different commitment primitive and a new
ADR specifies its security and compatibility consequences.

## ADR-010: complete-input accounting

**Status:** accepted

**Decision:** Bind a batch header to its canonical order Merkle root and require exactly one terminal
disposition for every committed leaf. Accounting records bind leaf position, order identifier, and
commitment, and must agree with the canonical allocation.

**Alternatives considered:** prove only included fills; audit aggregate order counts; accept arbitrary
record order without canonical leaf indices.

**Rationale:** Local conservation does not detect omitted orders. Complete-input accounting makes
operator censorship after commitment and fabricated dispositions observable to the verifier.

**Assumptions:** network ingress before an order enters the committed batch remains outside the
mechanism's guarantees.

**Evidence:** `cpp/src/engine/accounting.cpp`, `cpp/tests/test_commitments.cpp`,
`cpp/tests/test_proof.cpp`, and `experiments/results/attack_evidence_table.json`.

**Reconsider when:** the venue adds signed ingress receipts or distributed sequencing.

## ADR-011: blinded ledger commitments

**Status:** accepted

**Decision:** Compute pre- and post-state roots from account leaves ordered by account identifier.
Each leaf binds cash and inventory with a persistent 256-bit salt derived from a venue secret and
account identifier. Participants can verify an inclusion proof for their own account without
receiving the full ledger.

**Alternatives considered:** publish synthetic roots; hash the whole ledger as one blob; use unsalted
low-entropy balance leaves; publish per-account salts.

**Rationale:** State roots must identify the ledger while preventing straightforward enumeration of
plausible balances by observers. A Merkle structure supports participant-local verification.

**Assumptions:** the operator protects the venue secret; persistent salts reveal whether a leaf has
changed and do not hide account-set membership.

**Evidence:** ledger commitment code under `cpp/src/commitments/`, frozen commitment vectors,
participant proof tests, and `docs/LIMITATIONS.md`.

**Reconsider when:** per-batch unlinkability, multi-instrument portfolios, or operator-blind balances
enter scope.

## ADR-012: bound oracle snapshot and validation policy

**Status:** accepted

**Decision:** Validate reference snapshots against an explicit maximum age and price collar, then
commit both the snapshot and policy into the public proof statement. Represent the absent-oracle
case with a domain-separated marker.

**Alternatives considered:** commit price only; keep policy implicit; use a zero placeholder; accept
stale snapshots with warnings.

**Rationale:** A price is meaningful only with the policy under which it was admitted. Binding both
prevents an operator from silently widening freshness or collar limits after execution.

**Assumptions:** one authenticated snapshot per single-instrument batch is sufficient; publisher
authenticity and multi-source aggregation are outside the current implementation.

**Evidence:** `cpp/src/engine/oracle_validation.cpp`, oracle commitment vectors, `cpp/tests/test_proof.cpp`, and stale or
out-of-collar adversarial cases.

**Reconsider when:** signed multi-source feeds or cross-venue aggregation are implemented.

## ADR-013: transparent R1CS research harness, not a cryptographic proof

**Status:** accepted

**Decision:** Synthesize selected order, clearing, allocation, accounting, conservation, and oracle
relations into an in-tree R1CS model and check satisfiability locally. Describe the emitted
certificate as a transparent research artifact, never as zero knowledge, succinct, remotely sound,
or production ready.

**Alternatives considered:** claim security from satisfiability alone; serialize only public inputs;
adopt a proving backend before its assumptions and integration are measured.

**Rationale:** The harness measures constraint structure and checks that the encoded relations agree
with tested plaintext semantics. Without a cryptographic backend, an external verifier cannot rely
on the serialized certificate as a proof of witness possession.

**Assumptions:** the harness is used for engineering validation and measurement, not as a security
boundary.

**Evidence:** `cpp/src/proof/`, `cpp/tests/test_proof.cpp`, `docs/PROOF_STATEMENT.md`, and
`experiments/results/zk_proof_scaling_benchmark.json`.

**Reconsider when:** an integrated backend binds the full statement and witness, rejects fabricated
certificates, and supplies reproducible prover, verifier, size, memory, and setup measurements.

## ADR-014: native running-state accumulator

**Status:** accepted

**Decision:** Chain valid step statements with SHA-256 while requiring ledger-root continuity and
monotonic session indices. The verifier retains and replays the individual step certificates.

**Alternatives considered:** describe the chain as recursive proof composition; store independent
artifacts without a running digest; adopt folding before a backend evaluation.

**Rationale:** A non-commutative running digest detects reorder, omission, and mutation relative to
the supplied step sequence. It is useful history integrity plumbing without overstating succinctness
or external verification cost.

**Assumptions:** every step certificate remains available to the verifier; hash-chain integrity does
not replace cryptographic proof soundness.

**Evidence:** `cpp/src/proof/recursive/`, `cpp/tests/test_recursive_folding.cpp`, frozen session vectors,
and `docs/RECURSIVE_STATE_TRANSITION.md`.

**Reconsider when:** a measured incrementally verifiable construction replaces replay of every
step.

## ADR-015: deterministic simulation and bounded market-data replay

**Status:** accepted

**Decision:** Provide fixed-seed synthetic order flow, deterministic interval batching, and a
strictly bounded Nasdaq TotalView-ITCH 5.0 parser with a transactional Level-3 order book. Reject
invalid sides, zero values, over-execution, arithmetic overflow, truncation, and trailing payload
bytes before mutating state.

**Alternatives considered:** clock-seeded simulations; permissive parsing; live network ingestion;
partial mutation followed by recovery.

**Rationale:** Research artifacts must reproduce exactly, and malformed market-data input must fail
closed. A bounded offline subset is preferable to claiming unsupported feed coverage.

**Assumptions:** only the documented message subset is supported; simulator output does not establish
live-market quality.

**Evidence:** `cpp/src/simulator/`, `cpp/src/marketdata/`, `cpp/tests/test_simulator.cpp`,
`cpp/tests/test_marketdata.cpp`, and interval-matrix artifacts.

**Reconsider when:** authenticated live ingestion or broader message coverage has reproducible tests
and operational evidence.

## ADR-016: raw artifacts are the source of empirical claims

**Status:** accepted

**Decision:** Store machine-readable experiment results with environment metadata and reproduction
commands. Generate all current results through `./scripts/reproduce_all_experiments.sh`. Keep
measurement separate from interpretation and label modeled quantities explicitly.

**Alternatives considered:** prose-only benchmark claims; manually edited tables; notebooks with
hidden state; unstamped regenerated golden files.

**Rationale:** A reviewer should be able to reproduce a number and inspect the environment that
produced it. The repository must not turn prototype measurements into claims about production
throughput, market quality, or cryptographic performance.

**Assumptions:** results are specific to their recorded environment and workload.

**Evidence:** `experiments/MANIFEST.json`, `experiments/results/`, and
`scripts/reproduce_all_experiments.sh`.

**Reconsider when:** a public benchmark service provides immutable raw results and equivalent
provenance.

## ADR-017: spec-derived commitment conformance

**Status:** accepted

**Decision:** Maintain a test-only commitment reference derived from the published byte layouts and
Merkle rules rather than production headers. Change canonical encodings in the decision record,
production code, and reference code as separate review steps.

**Alternatives considered:** round-trip production tests; copy production helpers into test code;
treat frozen digests as independent implementation evidence.

**Rationale:** A disagreeing reference can catch production encoding drift. Sharing production
helpers would make both sides repeat the same defect.

**Assumptions:** the reference and production implementations still share one author and the in-tree
SHA-256 primitive, so their independence is limited.

**Evidence:** `cpp/tests/conformance/reference_commitments.hpp`,
`cpp/tests/test_conformance.cpp`, and frozen commitment vectors.

**Reconsider when:** an external implementation or standards suite provides stronger independent
conformance evidence.

## ADR-018: no production proving backend selected

**Status:** accepted

**Decision:** Do not select or advertise a zero-knowledge, succinct, or incrementally verifiable
backend until a reproducible spike binds the complete FairCross statement and measures integration
costs. Keep backend families as future research rather than dependencies.

**Alternatives considered:** choose from ecosystem reputation; benchmark an isolated toy circuit;
infer production performance from native constraint synthesis.

**Rationale:** Backend selection changes the threat model, field arithmetic, commitment design,
setup assumptions, supply chain, proof size, and verifier semantics. Those consequences require
evidence from the actual FairCross relation.

**Assumptions:** the current transparent harness remains explicitly non-cryptographic.

**Evidence:** `docs/research/zk_backend_evaluation.md`, `docs/PROOF_STATEMENT.md`, and
`docs/LIMITATIONS.md`.

**Reconsider when:** a candidate passes the selection evidence listed in the backend evaluation and
the repository gate reproduces its results.
