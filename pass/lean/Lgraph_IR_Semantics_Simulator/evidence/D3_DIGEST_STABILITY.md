# D3 — the 19 changed digests are unreproducible, and the retained evidence says digests track certificates

`PORT_LEDGER_D3_D4_FP.md` §27.2 recorded a fresh CORE-ET run against the
retained historical rows: 74 modules ran in both with a digest, 55 identical,
so **19 changed** — "reproducible and unexplained". It also said what would
settle it: *the SAME certificate and the SAME stimulus through the old and new
semantics, one variable at a time.*

That comparison cannot be run. This records why, and what the retained
evidence does establish instead.

## The run that produced the 19 was never retained

§27.2's fresh run reported **RAN 76 / NOT_RUN 46**. Three 122-module runs are
retained under `pass/lean/Lgraph_IR_Semantics_Simulator/tests/d4/`, and none of them is that run:

| rows file | `d4_head` | date | RAN | NOT_RUN |
|---|---|---|---:|---:|
| `coreet_122_runtime_rows.tsv` | `1a9505128` | 2026-10-02 12:35 | 82 | 40 |
| `coreet_122_fresh_rows.tsv` | `e4fb8c82d` | 2026-10-02 23:05 | 83 | 39 |
| `coreet_122_r2_rows.tsv` | `49cf29864` | 2026-10-03 03:23 | 93 | 29 |

So the 19 cannot be recomputed, or even re-listed: the rows that named them
are gone. Note also that the labels mislead — "historical r2" is the **newest**
of the three by four hours, not the oldest.

## The three runs that were retained show no digest movement at all

Pairwise, over every module that ran in both:

| pair | overlap | identical | changed |
|---|---:|---:|---:|
| runtime vs fresh | 80 | 80 | **0** |
| runtime vs r2 | 80 | 80 | **0** |
| fresh vs r2 | 83 | 83 | **0** |

Three different `lhd` binaries, and on every module where both runs produced a
digest, the certificate hash is identical too.

The one real difference between `fresh` and `r2` is coverage, not digests: ten
modules have a certificate in `r2` and none in `fresh` —
`minion_dcache_{buffer,metadata,replay_queue,tlb}_array`, `minion_tlb`,
`vpu_lane_tima`, and the four `vpu_tensor*_rf`. Every one is a memory design,
and `49cf29864` is "pass.single_edge: derive each Memory write port's commit
edge independently". That is the memory fix landing, not an answer changing.

## The invariant, over all retained rows

Across every D4 sweep row kept in the tree — CORE-ET and CVA6, every corpus,
every run:

    rows carrying both a certificate hash and a digest : 778
    distinct certificates                              : 130
    certificates that ever produced MORE THAN ONE digest : 0

On this evidence the digest is a function of the certificate. That is the
strongest statement available, and it makes "the 19 were 19 certificate
changes" the obvious reading — the fresh run differed in its `lhd` binary, so
it emitted different certificates. It is an inference, not a measurement,
because the run's rows are gone.

## Why the controlled replay cannot close it

`scripts/replay_d4_certs.sh` is exactly the experiment §27.2 asked for: it
holds the certificates fixed, refusing any whose SHA-256 does not match the
recorded table, and re-runs only `.dcert -> CertIO.loadCert -> runChecked`.

It has nothing to run. Of **524** `.dcert` files surviving on disk, **zero**
match any of the 130 recorded hashes. The certificates lived under
`generated/`, which is not tracked, and they are gone.

## What is still genuinely open

Every retained row used Lean **4.31.0**, which is still the pin today
(`formal/lean/lean-toolchain`). So the 130-for-130 invariant tests variation in
the EXPORTER only. Nothing retained exercises a change in the Lean semantics
against a fixed certificate, and this note does not claim one would preserve a
digest.

## The recurrence guard

A digest is only investigable while its certificate exists. Any sweep whose
rows are committed should either retain the `.dcert` files those rows name, or
say in the rows file that it did not — otherwise the next "reproducible and
unexplained" difference will be just as unreachable as this one.
