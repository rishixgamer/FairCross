# Development process

FairCross is maintained as a research artifact whose claims must remain narrower than its evidence.
Changes are accepted only when the deterministic C++ semantics, public documentation, and
reproducible artifacts agree.

## Verification discipline

Every consequential change should:

1. state the market, commitment, proof, or experiment invariant it affects;
2. add a focused regression test or property when behavior changes;
3. preserve canonical encodings and frozen vectors unless a prior design decision explicitly
   authorizes a semantic revision;
4. pass `./scripts/check.sh`; and
5. update `docs/DECISIONS.md` when the trade-off or evidence boundary changes.

Market semantics are defined and tested as deterministic plaintext C++ before they are represented
as proof constraints. Experiment summaries are secondary to the machine-readable artifacts and
their embedded reproduction commands.

## AI-assisted development

AI-assisted coding and review tools may be used during development. Their output is treated as an
untrusted proposal: it does not establish correctness, authorship of a claim, or completion of a
change. The maintainer owns the specification and acceptance decision, and executable tests,
sanitizers, conformance checks, frozen vectors, and reproducible experiments provide the reviewable
evidence.

Contributors should not commit private prompts, generated work logs, tool-specific instructions, or
temporary planning material. The public repository should contain the engineering artifact and the
evidence needed to evaluate it.
