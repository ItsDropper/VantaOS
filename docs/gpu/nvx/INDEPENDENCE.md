# NVX independence policy and hardware knowledge base

NVX is an independently engineered NVIDIA GPU driver. It is not a port, fork,
derivative or architectural clone of Nouveau, Mesa NVK, Linux Nova, or NVIDIA's
own open kernel modules, and its design follows VantaOS's GPU architecture
(`docs/gpu/FRAMEWORK.md`), not theirs.

## Rules

1. **No copying or translation.** Source from external drivers is never copied or
   rewritten in another form. A function-by-function restatement of an external
   implementation is a translation and is not allowed.
2. **External implementations are evidence, not authority.** Nouveau, NVK, Nova
   and NVIDIA's `.c` implementation files may suggest hardware behavior. A
   suggestion is recorded as `external-impl` and stays `unverified` until it is
   independently established (rule 4).
3. **Source classes.** Every fact has exactly one class:
   - `official-doc`: NVIDIA's published register/class/ABI headers and other
     vendor documentation.
   - `public-spec`: an industry standard (for example the PCI Firmware spec).
   - `external-impl`: behavior inferred from another driver's code or tables.
   - `experiment`: observed by NVX's own diagnostic tooling on real hardware.
4. **Independent establishment.** A fact is established by a controlled
   experiment NVX runs itself (see `nvx survey`, below), not by a second reading
   of the same external code. Official documentation is accepted as a source but
   is still recorded, and is verified by experiment where an experiment exists.
5. **Gate on writes.** A fact that is `external-impl`, still `unverified`, and
   gates a register write (`writes=1`) must not be trusted on hardware before an
   experiment establishes it. `scripts/nvx_knowledge.py report` lists these.
6. **Policy is not hardware.** Sizes and orderings chosen by a reference driver
   (heap sizes, reset ordering) are that driver's decisions. NVX designs its own
   and establishes the firmware's real minimums by experiment.
7. **Never claim more than the evidence.** Statuses are `unverified`,
   `observed-consistent` (a survey on a real GPU agreed), `contradicted`, and
   `hardware-verified` (behavior demonstrated end to end, such as a CE copy
   checked by the CPU).

## The knowledge base

`docs/gpu/nvx/knowledge/facts.tsv` is the single record of every hardware fact
NVX relies on: id, domain, name, register offset/mask, expected value predicate,
source class and reference, status, the code that uses it, whether the survey
reads it, and whether NVX writes it. Source files may cite a fact as
`NVX-F-NNNN`; CI rejects unknown ids.

```
python3 scripts/nvx_knowledge.py check          # schema, code refs, fact tags
python3 scripts/nvx_knowledge.py report         # the independence debt
python3 scripts/nvx_knowledge.py gen-survey     # regenerate include/nvx_survey_table.h
python3 scripts/nvx_knowledge.py analyze LOG    # judge a survey log (--write-status to record it)
```

## Controlled experiment 1: `nvx survey`

`nvx survey` reads (never writes) every register marked `survey=1` and prints one
line per observation to the terminal and serial port. `analyze` compares them
with each fact's expectation and reports CONFIRMED, CONTRADICTED, OBSERVED
(recorded, no expectation) or READ-FAILED. It refuses a log without a complete
begin/end block, ignores observations whose offsets disagree with the database,
and judges only the last survey in a log.

How much a CONFIRMED means: an expectation like "not a PRI error" shows the
register exists and answers; it does not prove the field meanings. The value
checks (`eq:`, `in:`) are stronger. Behavior facts (write ordering, firmware
ABI) need the later experiments below and are not covered by the survey.

## Audit of the current tree (31 facts)

| Source class | Facts | All status |
| --- | --- | --- |
| official-doc | 11 | unverified |
| public-spec | 1 | unverified |
| external-impl | 19 | unverified |

19 facts rest only on an external implementation, and 4 of those gate register
writes (Falcon CPUCTL writes, BROM programming order, Falcon reset ordering).
Two of NVX's recent modules were written from NVIDIA's reference code and are
flagged rather than hidden: `nvx_gsp_wpr_policy` (heap sizing, FRTS, region
arithmetic: NVX-F-0050/0051/0052) and the IFR/NPDE parts of
`nvx_gsp_vbios_prom` (NVX-F-0021/0023). Treat both as *reference-informed
hypotheses to be replaced or confirmed*, not as NVX knowledge. Their tests prove
internal consistency, not hardware truth.

## Experiments still needed (all require a physical or passed-through GPU)

| Id | Experiment | Establishes |
| --- | --- | --- |
| E1 | `nvx survey` on an RTX 3060 | register existence/identity facts above |
| E2 | Dump the PROM window to a file and parse it offline with independent tooling | IFR/NPDE layout (F-0021/0023), expansion-ROM rule |
| E3 | Read-only probe of Falcon/RISC-V state before and after firmware runs | Falcon status semantics, PRI-lock behavior (F-0037) |
| E4 | Run FWSEC FRTS with a range of FRTS sizes and observe the result registers | FRTS size requirement (F-0050) |
| E5 | Boot GSP with decreasing heap sizes and record where initialization fails | the firmware's real heap minimum (F-0051) |
| E6 | Vary BROM write order in isolation and observe boot outcome | whether order matters (F-0040) |
| E7 | First RPC round trip, then systematic field probing of the closed GSP-RM ABI | message/struct ABI toward the firmware |
| E8 | CE copy verified byte for byte by the CPU | `hardware-verified` for the first engine path |

Closed firmware (GSP-RM, FWSEC, Booter) cannot be understood from the host side
alone. Its interfaces are established by observing its behavior under
controlled input on hardware, and every result is recorded in the knowledge base
with its experiment id. No such run has happened yet.
