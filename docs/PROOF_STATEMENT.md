# FairCross Single-Batch Transparent Constraint Statement v0

## Status

This document describes the relation checked by the in-tree C++20 R1CS research harness. It is not
a zero-knowledge proof, SNARK/STARK, succinct proof, or remote malicious-prover security boundary.
`SingleBatchProver` possesses the full witness and checks the relation locally;
`SingleBatchVerifier` checks public-input equality and certificate formatting only.

## Relation

For one canonical batch, the harness checks that a private execution witness is consistent with the
published pre/post ledger roots, batch header, optional oracle commitment, clearing price, and
cleared volume:

```text
check(pre_state_root, post_state_root, batch_header_hash,
      oracle_snapshot_hash, clearing_price, cleared_volume; witness)
```

## Public inputs

| Field | Representation | Bound meaning |
|---|---|---|
| `pre_state_root` | 32-byte `Commitment` | Salted Merkle root of the pre-batch ledger |
| `post_state_root` | 32-byte `Commitment` | Salted Merkle root of the post-batch ledger |
| `batch_header_hash` | 32-byte `Commitment` | Version, batch id, instrument, cutoff, order count, and order Merkle root |
| `oracle_snapshot_hash` | 32-byte `Commitment` | Snapshot plus freshness/collar policy, or the explicit absent marker |
| `clearing_price` | `uint64_t` | Uniform clearing price; zero when the book does not cross |
| `cleared_volume` | `uint64_t` | Executable quantity in integer lots |

The full commitments are checked by the plaintext prover boundary. Selected low 64-bit scalar
projections enter the educational `ConstraintSystem`; this is another reason the current artifact
must not be treated as a production cryptographic proof.

## Private witness

`BatchProofWitness` carries:

- pre- and post-state ledgers;
- the canonical `Batch`;
- one salted order preimage per canonical batch leaf;
- deterministic allocation and canonical fills;
- complete-input accounting records;
- an optional oracle snapshot and policy;
- the batch cutoff used by the header and freshness relation.

## Checks performed by `SingleBatchProver`

1. Preimage count equals batch length, and each preimage order equals the canonical batch order at
   the same leaf index.
2. The version-1 batch header is reconstructed from witness data and must hash to the published
   `batch_header_hash`.
3. Accounting records cover every leaf exactly once and bind leaf index, order id, and commitment;
   the Merkle tree cannot contain extra leaves.
4. `verify_transition` recomputes the full canonical allocation and fill vector, applies the fills,
   and checks account-level settlement plus cash/inventory conservation.
5. Pre/post ledger roots and the optional oracle commitment are recomputed from the witness.
6. The in-tree R1CS subrelations for order validity, input accounting, clearing optimality,
   allocation, fill bounds, conservation, and oracle policy are synthesized and checked locally.

Untrusted file-backed batches enter through `Batch::create`, which rejects instrument mismatch,
duplicate ids, invalid sides, zero prices, and zero quantities.

## Transparent certificate boundary

The emitted bytes have the deterministic form:

```text
FC-R1CS-V1:constraints=<count>:public_inputs_len=5:status=SAT
```

This records that the local prover completed. Because the certificate contains neither the witness
nor a cryptographic proof, an external party can fabricate a well-formed string. The verifier's
format check does not establish R1CS satisfaction. Closing that gap requires either a
witness-carrying transparent proof that replays the relation or a separately selected sound proving
backend. See [`LIMITATIONS.md` section 2.9](LIMITATIONS.md).

## Explicit non-claims

The current harness does not establish:

- zero knowledge or witness privacy;
- succinctness, proof soundness, or production cryptographic security;
- authenticated network arrival before batch cutoff;
- oracle-publisher authenticity;
- external solvency, custody, regulatory compliance, or live-market correctness;
- prevention of collusion, wash trading, self-trading, or pre-root network censorship.
