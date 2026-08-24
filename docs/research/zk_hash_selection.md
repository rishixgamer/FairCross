# Future proof-oriented hash evaluation

**Status:** open research question

## Current position

FairCross uses domain-separated SHA-256 commitments and Merkle trees. The implementation is in-tree,
pinned against published digest vectors, and checked by a spec-derived conformance reference. No
proof-oriented algebraic hash has been selected or implemented.

The transparent R1CS harness does not claim that SHA-256 is efficiently represented inside a
production circuit. Its current constraint measurements describe the relations actually synthesized
by the repository, not a complete cryptographic hash gadget.

## Candidate requirements

Any future hash selection must evaluate:

1. security assumptions and available cryptanalysis;
2. compatibility with the selected proving field and backend;
3. constraints or rows for order leaves, ledger leaves, and binary Merkle nodes;
4. canonical mapping of FairCross integer and byte encodings into field elements;
5. domain separation for every commitment type;
6. native C++ integration and dependency pinning;
7. published parameter generation and test vectors; and
8. transition rules for already committed batches and frozen vectors.

## Decision gate

A candidate may be adopted only after a reproducible spike implements the complete FairCross leaf
and Merkle relation, measures proof-system costs, documents security parameters, and passes an
independent encoding-conformance review. Until then, SHA-256 remains the only implemented commitment
primitive and candidate comparisons remain future research rather than architecture commitments.
