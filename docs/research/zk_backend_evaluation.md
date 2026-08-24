# Future ZK and IVC Backend Evaluation

**Date:** 2026-08-20  
**Status corrected:** 2026-08-24

## Current decision

FairCross has **not selected or implemented a zero-knowledge or IVC proving backend**. The C++20
tree has no external proving dependency. Its in-tree R1CS layer synthesizes and checks constraints
locally, and its session layer is a native hash-chain accumulator. Neither artifact is a sound
remote proof, a zero-knowledge proof, or a recursive proof.

## Candidates retained for future evaluation

| Family | Potential fit | Integration risks to resolve |
|---|---|---|
| Pairing-based R1CS proof | Direct mapping from the current constraint model | Trusted setup, verifier model, commitment gadgets, dependency pinning, and C++ integration |
| Folding or incrementally verifiable computation | Natural fit for repeated state transitions | Curve-cycle assumptions, final compression, witness design, and empirical cost |
| STARK-oriented recursion | Transparent setup and recursion research | Field mismatch, non-native hashing/range checks, integration maturity, and proof-size cost |
| Lookup-oriented proving system | Efficient range checks and custom relations | Circuit complexity, setup choice, recursion design, and C++ integration |

This table is a research shortlist, not a dependency or architecture commitment. Qualitative claims
about project maintenance or production use must be refreshed from primary sources if evaluation
resumes.

## Required selection evidence

A backend may be selected only after a new reproducible spike demonstrates:

1. the full FairCross statement and witness are bound to the proof;
2. malicious but well-formed fabricated certificates fail verification;
3. the chosen commitment and state-transition encodings match the published ADRs;
4. proof size, prover time, verifier time, peak memory, and setup assumptions are measured;
5. recursive or folding verification is measured over increasing session length;
6. dependencies are pinned and the C++ integration is supported by the master gate.

Until then, all backend-specific latency, constraint, MSM, and constant-verification numbers are
unmeasured future-work hypotheses rather than FairCross results.
