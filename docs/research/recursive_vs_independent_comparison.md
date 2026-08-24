# Comparative Evaluation: Native History Accumulator vs. Independent Batch Artifacts

## 1. Scope and interpretation

This report compares the storage and runtime behavior of the current in-tree artifacts. The
"recursive" path is a native SHA-256 hash-chained running-state accumulator; it is not recursive
IVC, a SNARK/STARK, a zero-knowledge proof, or a succinct cryptographic proof. The verifier replays
the individual step proofs and rebuilds the accumulator. Claims below are limited to the recorded
benchmark and do not extrapolate to a future proving backend.

Measurements come from `cpp/bin/faircross_bench recursive-vs-independent` on the recorded Apple
Silicon (`aarch64-apple-darwin`) build. The authoritative artifact is
`experiments/results/zk_recursive_vs_independent_comparison.json`.

## 2. What the comparison measures

| Metric | Independent per-batch artifacts | Native running accumulator | Interpretation |
|---|---:|---:|---|
| Stored value at session boundary | $O(K)$ total artifact bytes | $O(1)$ accumulator certificate bytes | Storage comparison only; `SessionProof` also retains step proofs |
| Prover work | $O(K)$ recorded work | $O(K)$ recorded work plus accumulator updates | Folding does not remove step computation |
| Verification | Per-step artifact checks | Per-step replay plus hash-chain rebuild | No constant external verification claim |
| Privacy / zero knowledge | None supplied by this artifact | None supplied by this artifact | Order and witness privacy require a future backend |

## 3. Complete measured table

| Session length $K$ | Independent bytes | Native accumulator certificate bytes | Independent/native ratio | Independent prove time ($\mu$s) | Accumulator-path prove time ($\mu$s) | Independent verify time ($\mu$s) | Accumulator-path verify time ($\mu$s) |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 56 | 120 | 0.466 | 15 | 31 | 0 | 0 |
| 2 | 112 | 120 | 0.933 | 30 | 63 | 0 | 1 |
| 5 | 280 | 120 | 2.333 | 77 | 155 | 0 | 2 |
| 10 | 560 | 122 | 4.590 | 153 | 313 | 0 | 5 |
| 20 | 1,120 | 122 | 9.180 | 308 | 628 | 0 | 9 |
| 50 | 2,800 | 122 | 22.950 | 799 | 1,621 | 0 | 23 |

The ratio column is the artifact's `storage_compression_ratio_permille` divided by 1,000. At
$K=50$, the measured comparison is therefore 2,800 independent bytes versus 122 native
accumulator-certificate bytes, or 22.95x.

## 4. Findings and limits

The accumulator gives a compact, deterministic record that is sensitive to order, price, volume,
and ledger-root history changes. It does not make the underlying step checks disappear: the current
verifier checks every step proof and continuity, and `SessionProof` contains those step proofs.
Consequently, this experiment supports a storage/data-structure result, not claims of constant
verification, recursive soundness, succinctness, or witness privacy.

A production recursive proof system remains a research question. It would need a specified proving
backend, independent soundness analysis, explicit public/witness boundaries, and new measurements
that are kept separate from these transparent in-tree results.
