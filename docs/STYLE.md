# Documentation style guide

VantaOS documentation exists so that a reader can tell **what is true, how we
know, and what is still unknown**. These rules keep it that way.

## Status vocabulary

Use exactly these words for the maturity of a feature or fact. They match the
[engineering model](architecture/ENGINEERING_MODEL.md) and the NVX
[readiness gates](gpu/nvx/RTX3060_READINESS.md).

| Term | Meaning | Does **not** mean |
| --- | --- | --- |
| **Planned** | Written down only | Any code exists |
| **Implemented** | Code exists and is in the production build | It was run, or works |
| **Host-tested** | An automated test on the build host exercises it with synthetic inputs | The real device behaves the same |
| **Emulated** | Runs against an emulator that models the device contract (QEMU for PC hardware; no NVIDIA GPU emulator exists) | Physical hardware works |
| **Hardware-verified** | A physical device produced a result independently checked | It works on other revisions or in all states |

Never write "supported", "working" or "complete" without naming the level and
the test or log that proves it.

## Hardware facts (NVX)

Facts about closed hardware carry a source class and status in the
[knowledge base](gpu/nvx/INDEPENDENCE.md): `official-doc`, `public-spec`,
`external-impl`, `experiment`; status `unverified`, `observed-consistent`,
`contradicted`, `hardware-verified`. Prose docs must not state an
`external-impl` fact as established.

## Document structure

1. **Title and one-paragraph purpose.** Who should read it and why.
2. **Status line** near the top for any document describing a subsystem:
   `Status: <level>. Last reviewed: YYYY-MM-DD.`
3. **Body** ordered from what a reader needs first (concepts, usage) to what
   they need last (internals, history).
4. **Limitations / not established** section. Mandatory for status documents.
5. **Related documents** links at the end if the index does not already make
   the relationships obvious.

## Writing rules

- Describe the current state in the present tense. Put history in git, not in
  the prose; remove "now", "recently", "newly added" wording that will rot.
- Prefer a table when comparing items across the same attributes, a numbered
  list for ordered procedures, prose for reasoning.
- Name commands, registers, files and symbols in `code` font; put multi-line
  commands in fenced blocks with a language tag.
- Give runnable commands. State where they run (host, QEMU guest, VantaOS shell).
- Warn before destructive or risky steps, not after.
- Link instead of copying; one fact has one home.
- Use relative links. `python3 scripts/check_doc_links.py` validates them and
  in-page anchors; it runs in CI.
- Spell out acronyms on first use or link to the [glossary](GLOSSARY.md).
- Do not paste literal escape sequences such as `\n` into Markdown.

## File placement

| Location | Content |
| --- | --- |
| `docs/architecture/` | How the OS is structured and its interfaces |
| `docs/build/` | Build, test, CI, installer, recovery |
| `docs/project/` | Plans, status, hardware support |
| `docs/gpu/` | GPU framework and vendor work (`nvx/` for NVIDIA) |
| `docs/decisions/` | Architecture decision records |
| `docs/*.md` | Legacy location; new documents go in a subdirectory |

When a document moves, leave a three-line `Moved:` pointer at the old path.
