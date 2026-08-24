# Threat model

## Assets to protect

- private limit prices;
- private quantities;
- participant identity/linkage where the selected privacy model supports it;
- balances and inventory beyond necessary disclosures;
- integrity of the auction result;
- integrity/completeness of batch input accounting.

## Adversaries

### A1: malicious operator

The venue may attempt to:

- omit an eligible committed order;
- insert an uncommitted order;
- use a stale or unauthorized reference price;
- choose a clearing price inconsistent with the published algorithm;
- preferentially allocate fills;
- overfill an order;
- create cash or inventory;
- mutate state outside the published transition.

The current plaintext checker rejects these transition classes within the implemented single-batch
model. The transparent R1CS harness cross-checks selected relations, but it is not a cryptographic
proof against a remote malicious operator.

### A2: malicious participant

A participant may submit:

- malformed orders;
- zero/negative-equivalent quantities;
- invalid price ticks;
- orders exceeding available buying power or inventory;
- duplicate/replayed identifiers;
- invalid signatures/authorization where authentication is implemented.

`Batch::create` rejects instrument mismatch, duplicate identifiers, invalid sides, zero prices, and
zero quantities before a batch reaches the engine. Authentication and buying-power enforcement are
not implemented in the current prototype.

### A3: malicious observer

An observer sees public market outputs, commitments, and proofs, and may try to infer individual
order sizes, limit prices, or participant identities from them, including the parameters of orders
that never executed.

What the commitment scheme currently provides:

- 256-bit blinding nonces on order commitments give statistical hiding in the commitments;
- 256-bit blinding salts on ledger account leaves (ADR-011) hide balances and inventory behind the
  published state roots. The salt is derived from an operator venue secret, so an observer without
  that secret cannot confirm a guessed balance. With unsalted leaves, a two-account ledger was
  recoverable from its published root in 2.2 seconds using only the conservation totals;
- the in-tree R1CS/certificate harness does **not** provide zero knowledge. A production proving
  backend would be required before witness privacy or remote proof soundness can be claimed.

Limits of the balance guarantee:

- it hides balances from observers, not from the operator, who holds the venue secret and the ledger
  it maintains;
- it blinds balances, not the membership of the account set: an observer who knows the account ids
  still learns how many accounts exist;
- the salt is stable across batches, so an observer learns whether a given account's balance
  *changed* between two roots, though not to what. Per-batch unlinkability is out of scope for v0
  (ADR-011).

An observer also sees whatever the protocol intentionally publishes. FairCross does not claim that a
future ZK layer alone prevents leakage from timing, aggregate volume, fills, network metadata, or
external market impact.

### A4: malicious oracle or reference feed

A compromised or malicious oracle may attempt to publish delayed or fabricated reference prices.

Assumptions:

- a deployment would require external reference price feeds (for example NBBO or consolidated price
  feeds) to be authenticated by authorized publishers. The prototype does not implement publisher
  signatures.
- the exchange verifies integer nanosecond timestamps against the deterministic batch cutoff:
  `cutoff_nanos - timestamp_nanos <= max_staleness_nanos`.
- reference prices are applied as price collar constraints (`|P* - P_ref| <= max_deviation_ticks`)
  rather than replacing endogenous batch auction crossing.

What the current prototype provides:

- any reference snapshot with an expired timestamp, or a timestamp in the future, is rejected before
  batch execution;
- collars bound how far the clearing price can sit from the reference, while order-driven price
  discovery continues inside the collar band.

## Trust boundaries

Early versions trust the process holding plaintext order data. This is acceptable for proving
**verifiable execution integrity**, but it does not provide operator-blind privacy.

MPC/threshold encryption is a later extension if the project expands the threat model to hide order
attributes from the operator itself.

## Explicit non-claims

Until supported by implementation and analysis, do not claim:

- information-theoretic privacy;
- resistance to all side channels;
- MEV/front-running elimination;
- regulatory compliance;
- production-grade cryptographic security;
- fault tolerance under arbitrary network partitions;
- operator blindness.
