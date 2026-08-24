# Research Plan

## Primary question

What is the computational cost of proving procedural fairness for a private frequent-batch market,
and which fairness guarantees dominate that cost?

## Candidate hypotheses

H1. Proving simple validity/conservation scales materially better than proving complete auction
optimality and allocation.

H2. Batch size creates a measurable trade-off between proof cost/latency and the market-design
benefits of batching.

H3. A future incrementally verifiable construction may reduce retained history artifacts and
verification work relative to independent proofs, subject to soundness and prover overhead.

H4. Complete-input accounting is a load-bearing fairness property: a proof of "correct execution on
some hidden subset" is insufficient against a malicious venue.

## Core experiments

### E1 — Plaintext engine correctness

Property-based testing across generated order sets.

### E2 — Proof scalability

For supported batch sizes, measure:

- constraint count or backend-equivalent complexity;
- prove time;
- verify time;
- peak memory;
- proof size.

### E3 — Attack matrix

For each malicious operator, record whether generation/verification rejects the transition and at
which invariant.

### E4 — Recursion

Compare independent batch proofs versus a folded/running history representation.

### E5 — Market mechanism

Vary batch interval in simulation and measure:

- fill rate;
- time to fill;
- executable volume;
- implementation shortfall/reference-price deviation where modeled;
- proof generation load per unit wall-clock time.

## Reproducibility

Each experiment must produce:

- machine-readable raw output;
- git commit hash if available;
- timestamp;
- hardware/OS/compiler version;
- command line;
- configuration;
- no manually edited benchmark values.

## Artifact provenance

Every empirical claim must name the artifact it came from, and every artifact must carry the environment that produced it.

| Set | Produced by | Reproduce with |
|---|---|---|
| `experiments/results/*` | `cpp/bin/faircross_bench` | `./scripts/reproduce_all_experiments.sh` |

Each artifact embeds an `environment` block and a `reproduction_command`. Timing columns are only comparable across runs of the *same* build configuration: the benchmark binary is compiled at `-O2` without sanitizers, while the test binary runs under ASan and UBSan. A latency figure taken from a sanitized build measures the sanitizer, not the implementation.

Structural columns — constraint counts, variable counts, proof sizes — are deterministic and must not move between runs. If one does, it is a semantic change, not benchmark noise, and it must be explained before the artifact is republished.
