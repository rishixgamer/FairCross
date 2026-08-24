# FairCross Project Specification

## Problem

Private trading venues need to conceal sensitive order information, but secrecy creates a trust
problem: participants cannot directly inspect whether the operator included all eligible orders,
used the promised reference price, respected the allocation rule, or conserved balances.

FairCross explores whether a venue can keep order attributes private while emitting cryptographic
evidence that its published market procedure was followed.

## Research question

Can a private frequent-batch market provide compact, publicly checkable evidence of procedural
fairness—order inclusion/accounting, deterministic clearing, valid allocation, reference-price
integrity, and conservation—without revealing individual order prices and quantities?

## MVP

The first complete vertical slice is deliberately small:

- one instrument;
- one closed batch at a time;
- limit buy/sell orders;
- integer price ticks and integer quantities;
- deterministic uniform-price clearing;
- deterministic allocation;
- account balances and inventory;
- explicit transition from pre-state to post-state;
- comprehensive tests.

No ZK proof is needed for the MVP. The plaintext state machine must be correct first.

## Target end state

A later FairCross version should support:

- cryptographic order commitments and batch roots;
- proof that every eligible commitment is accounted for;
- proof of order validity and balance sufficiency;
- proof of deterministic auction clearing/allocation;
- proof of cash/asset conservation;
- authenticated/fresh reference-price constraints where the mechanism uses a reference;
- adversarial operators that intentionally violate one invariant at a time;
- recursive folding of successive valid exchange transitions;
- reproducible microstructure and proving-performance experiments.

## Non-goals

Initially, FairCross is not:

- a production broker/dealer or exchange;
- a live-money trading system;
- a blockchain protocol;
- an HFT strategy;
- an alpha predictor;
- a claim of legal/regulatory compliance;
- a claim that cryptography removes every information-leakage channel.

## Success criteria

A technically credible release should let another engineer:

1. reproduce the deterministic clearing result;
2. run property tests for core invariants;
3. run malicious transitions and observe rejection;
4. reproduce proof/verification benchmarks when the proof layer lands;
5. reproduce the main experimental tables/figures from documented commands.
