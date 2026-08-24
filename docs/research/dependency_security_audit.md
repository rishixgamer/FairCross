# FairCross Dependency, Supply-Chain, and License Security Audit

## 1. Audit Scope & Objectives

This audit examines the complete supply-chain posture, direct and transitive dependencies, cryptographic pinning, license compatibility, and minimal feature attack surfaces for the **FairCross** implementation.

Under the repository's research guidelines:
- Dependencies are strictly minimal;
- Cryptographic primitives are pinned and audited;
- Floating-point arithmetic is excluded from exchange state;
- Everything shipped is in-tree and readable, with no external licensing obligations.

---

## 2. Dependency Inventory & License Review

There are **no third-party dependencies**, direct or transitive. Every capability that would
normally be imported is implemented in-tree against a published specification:

| Capability | Implementation | Specification pinned against |
|---|---|---|
| SHA-256 order commitments & Merkle trees | `cpp/src/commitments/sha256.cpp` | FIPS 180-4, checked against published NIST test vectors |
| JSON fixture reading | `cpp/include/faircross/util/json.hpp` | Trusted local fixtures only; explicitly not hardened against adversarial input |
| JSON artifact & experiment report export | `cpp/include/faircross/util/json_writer.hpp` | Published output schema |
| Property-based fuzzing of market invariants | `cpp/tests/property.hpp` | Test-only; deterministic seed, greedy shrinking |

The in-tree JSON reader is the one place where this posture carries a caveat that matters: it is
written for trusted local fixture files, and deeply nested input can exhaust the stack. It must not
be pointed at untrusted data.

---

## 3. Cryptographic Posture & Version Pinning

1. **SHA-256 Pinning**:
   - The implementation is in-tree and pinned against published NIST FIPS 180-4 test vectors.
   - The Merkle tree and commitment algorithms use standard RFC 6234 / FIPS 180-4 padding and domain separation (`FC-ORDER-COMMIT-V1`, `FC-MERKLE-LEAF-V1`, `FC-MERKLE-NODE-V1`, `FC-ORACLE-V1`).
2. **Zero Unvetted Frameworks**:
   - In adherence to engineering rules, external experimental proof frameworks (Nova, arkworks, Plonky2, Halo2) have not been prematurely introduced into the core state machine, keeping the codebase self-contained, auditable, and platform-portable.

---

## 4. Static Analysis & Compilation Flags

The build treats warnings as defects and runs the full test suite under two sanitizers:
```
-std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wdouble-promotion
-fsanitize=address,undefined
```
- **Memory and UB checking**: ASan and UBSan run over every test in the gate. `-Wconversion`
  catches the silent narrowing that integer market arithmetic is most exposed to.
- **Zero Floating-Point Arithmetic in exchange state**: enforced by the fixed-point `Price`,
  `Qty`, and `Money` types, which expose no floating-point conversion, and by review.

  **This is a weaker guarantee than it may appear, and the difference should not be glossed.**
  `-Wdouble-promotion` catches only accidental promotion to `double`; there is no compiler flag
  that bans floating-point arithmetic outright, so the no-float rule is enforced by the type
  system and by reviewers rather than mechanically across the whole tree. A float introduced in
  code that does not touch the domain types would not be rejected by the build.

---

## 5. Audit Conclusion

The FairCross dependency posture is lean to the point of being trivial to audit: the implementation has **no third-party runtime dependencies at all**. SHA-256 is implemented in-tree and pinned against published NIST vectors. There is consequently no dependency tree to carry vulnerabilities, license conflicts, or unmaintained packages.

The trade-off is stated rather than hidden: an in-tree cryptographic primitive receives far less external scrutiny than a widely used library. SHA-256 is pinned against NIST test vectors, but the conformance harness reuses that same implementation, so a defect inside it would be invisible to the harness (`docs/LIMITATIONS.md` §7).
