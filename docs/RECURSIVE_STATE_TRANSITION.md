# FairCross Native Running-State Accumulator

## Status

FairCross currently implements deterministic native history accumulation, not Incrementally
Verifiable Computation (IVC), recursive SNARK verification, or a constant-size proof of an entire
session. `SessionProof` retains every transparent step certificate, and the verifier processes those
steps linearly.

## Running state

After batch `k`, the public state is:

```text
S_k = (ledger_root_k,
       history_accumulator_k,
       batch_id_k,
       timestamp_nanos_k,
       instrument_id)
```

The history accumulator is updated as:

```text
H_k = SHA256("FC_LEAF_" ||
             H_(k-1) || batch_header_hash_k ||
             clearing_price_k_le64 || cleared_volume_k_le64)
```

The exact outer commitment behavior follows the in-tree `Sha256Scheme`; the formula above is a
readable description, not an independent encoding specification.

## Prover-side fold relation

`RunningState::fold_step` has the full `BatchProofWitness` and rejects a step unless:

1. `batch_id_k == batch_id_(k-1) + 1`;
2. the instrument is unchanged;
3. the timestamp is monotonic;
4. the step's pre-state root equals the prior running ledger root;
5. `SingleBatchProver` accepts the bound transparent single-batch relation.

It then updates the ledger root and history accumulator. `RecursiveSessionProver` repeats this for
every step and stores each resulting `BatchProof` in the `SessionProof`.

## Verifier behavior

`RecursiveSessionVerifier`:

- checks the supplied initial/final session public inputs;
- requires a non-empty step vector whose size equals `num_batches`;
- checks each step's public inputs and exact transparent certificate grammar;
- checks consecutive ledger-root continuity;
- recomputes the history accumulator;
- compares the final ledger root and accumulator with the expected final state.

Because step certificates do not carry a witness or cryptographic proof, the verifier cannot
independently establish the step's R1CS satisfaction, batch-id/timestamp/instrument witness
continuity, or witness privacy. A fabricated well-formed step certificate remains possible. The
native accumulator detects mutation relative to a trusted sequence of step public inputs; it is not
a remote proof of how those inputs were produced.

## Measured artifact interpretation

At `K=50`, the current raw comparison records 2,800 bytes of independent per-batch artifacts and a
122-byte native accumulator certificate, a 22.95x ratio. The in-memory/on-wire `SessionProof` also
retains its 50 step proofs, and verification remains linear. The 122-byte field must not be described
as a complete constant-size recursive proof.

Source: [`experiments/results/zk_recursive_vs_independent_comparison.json`](../experiments/results/zk_recursive_vs_independent_comparison.json).

## Future proof-system gate

A genuine recursive proof requires a dedicated ADR that selects and pins the backend, defines the
field and commitment mappings, proves the already-tested step relation, publishes soundness/privacy
assumptions, and adds independent verification vectors. No Nova, Halo2, Groth16, or other backend is
currently part of the repository.
