# FairCross Adversarial Operator Attack Evidence Report

**Summary:** Evaluated 12 vectors (11 malicious mutations, 1 honest control). All invariants strictly held (12/12 secure).

| ID | Attack Vector | Target Invariant | Expected Outcome | Engine Rejection Reason | ZK Proof Behavior | Status |
|---|---|---|---|---|---|---|
| **ATK-001** | honest_baseline | All Market Invariants | Accept (Honest) | `None (Honest baseline)` | R1CS Proof Satisfied & Verified | **PASSED** |
| **ATK-002** | omitted_order_censorship | Complete-Input Accounting (Merkle Inclusion) | Reject (Malicious) | `AccountingError: accounting count mismatch: expected 2, got 1` | R1CS Merkle Root Authentication Unsatisfiable | **PASSED** |
| **ATK-003** | clearing_price_inflation | Deterministic Auction Clearing Price Rule | Reject (Malicious) | `InvariantViolation: non-deterministic clearing price: expected Some(Price(100)), got Some(Price(105))` | R1CS Uniform Clearing Price Constraint Unsatisfiable | **PASSED** |
| **ATK-004** | clearing_price_deflation | Deterministic Auction Clearing Price Rule | Reject (Malicious) | `InvariantViolation: non-deterministic clearing price: expected Some(Price(100)), got Some(Price(95))` | R1CS Uniform Clearing Price Constraint Unsatisfiable | **PASSED** |
| **ATK-005** | overfill_attack | Quantity Upper Bound Conservation | Reject (Malicious) | `InvariantViolation: allocation rule mismatch on order 1: expected 20, got 99999` | R1CS Fill Upper Bound Constraint Unsatisfiable | **PASSED** |
| **ATK-006** | preferential_allocation | Pro-Rata Largest Remainder Determinism | Reject (Malicious) | `InvariantViolation: allocation rule mismatch on order 1: expected 20, got 1` | R1CS Pro-Rata Remainder Allocation Equality Unsatisfiable | **PASSED** |
| **ATK-007** | limit_price_violation | Limit Price Protection Boundary | Reject (Malicious) | `InvariantViolation: limit price violated on buy order 1: limit 100, executed @ 99999` | R1CS Limit Price Inequality Unsatisfiable | **PASSED** |
| **ATK-008** | frozen_post_state_attack | Market Microstructure Invariants | Reject (Malicious) | `InvariantViolation: account state mismatch for account 1: cash does not match pre-state updated by this account's fills` | R1CS Constraint Failure | **PASSED** |
| **ATK-009** | cash_creation_attack | Cash Balance Conservation | Reject (Malicious) | `InvariantViolation: cash conservation violated: pre 15000, post 25000` | R1CS Ledger Debit/Credit Equality Unsatisfiable | **PASSED** |
| **ATK-010** | inventory_creation_attack | Asset Inventory Conservation | Reject (Malicious) | `InvariantViolation: asset conservation violated for 1: pre 50, post 100` | R1CS Ledger Asset Balance Equality Unsatisfiable | **PASSED** |
| **ATK-011** | commitment_leaf_spoofing | Cryptographic Order Commitment Integrity | Reject (Malicious) | `AccountingError: Merkle inclusion proof verification failed for leaf 0` | R1CS Merkle Path Authentication Unsatisfiable | **PASSED** |
| **ATK-012** | stale_oracle_reference | Reference Price Freshness & Collar Policy | Reject (Malicious) | `OracleValidationError: reference snapshot is stale: age 1000000001ns > max 1000000000ns` | R1CS Oracle Timestamp & Collar Inequality Unsatisfiable | **PASSED** |
