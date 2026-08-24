#!/usr/bin/env python3
"""Verify the frozen golden vectors that pin FairCross market and proof semantics.

`fixtures/golden/expected/` holds frozen reference values: clearing outcomes,
allocations, fills, post-state, batch header commitments, R1CS constraint counts,
commitment-scheme digests, and recursive session accumulators. The C++ suite in
`cpp/tests/test_golden_vectors.cpp` asserts byte-for-byte agreement against them.

These files are *frozen*, not regenerated. They record the values published for a
named release, so they detect drift from a recorded value rather than disagreement
between two live implementations. That distinction is deliberate and is recorded
in `docs/LIMITATIONS.md`.

This script is the gate's guard against a vector file going missing, becoming
unparseable, or losing its provenance stamp -- any of which would silently turn a
parity assertion into a skipped one.

Usage:  python3 scripts/verify_golden_vectors.py
"""

import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
GOLDEN = ROOT / "fixtures" / "golden"
EXPECTED = GOLDEN / "expected"

# Every fixture must have an expectation, plus the two primitive vector files.
# The batch-level expectations exercise the commitment scheme only through whole
# fixtures; `commitment_vectors.json` pins the primitives directly, including the
# odd-leaf Merkle padding path that reaches `empty_node()`.
STANDALONE = ["commitment_vectors.json", "session_vectors.json"]

REQUIRED_STAMP_FIELDS = ["status", "frozen_for_release"]


def main() -> None:
    if not EXPECTED.is_dir():
        sys.exit(f"missing expectation directory: {EXPECTED.relative_to(ROOT)}")

    fixtures = sorted(GOLDEN.glob("*.json"))
    if not fixtures:
        sys.exit("no golden fixtures found")

    required = [f.name for f in fixtures] + STANDALONE

    missing = [name for name in required if not (EXPECTED / name).exists()]
    if missing:
        sys.exit("frozen vectors are missing: " + ", ".join(sorted(missing)))

    failures: list[str] = []
    for name in required:
        path = EXPECTED / name
        try:
            doc = json.loads(path.read_text())
        except json.JSONDecodeError as exc:
            failures.append(f"{name}: not valid JSON: {exc}")
            continue

        prov = doc.get("_provenance")
        if not isinstance(prov, dict):
            failures.append(f"{name}: UNSTAMPED (no _provenance object)")
            continue

        absent = [f for f in REQUIRED_STAMP_FIELDS if not prov.get(f)]
        if absent:
            failures.append(f"{name}: incomplete provenance, missing {', '.join(absent)}")
            continue

        release = str(prov["frozen_for_release"])
        print(f"  frozen {name}  ({prov['status']} for {release})")

    if failures:
        for line in failures:
            print(f"  FAIL {line}", file=sys.stderr)
        sys.exit(f"{len(failures)} golden vector file(s) failed verification")

    print(f"\n{len(required)} frozen vector files verified.")


if __name__ == "__main__":
    main()
