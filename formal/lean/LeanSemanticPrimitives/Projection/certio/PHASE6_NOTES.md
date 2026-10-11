# Phase 6, first infrastructure increment — what was run, and where it stopped

Reproduce everything here with

    bash formal/lean/scripts/run_cert.sh --emit <dir>        # fixtures as .dcert
    bash formal/lean/scripts/run_cert.sh <file.dcert> [cycles] [seed]
    bash formal/lean/LeanSemanticPrimitives/Projection/certio/check_certio.sh

## Pipeline smoke, on the two shared fixtures — PASS

Both paths run on identical deterministic stimuli (`CertIO.stimulus`), and the
digests are compared.

    file        bytes  src  nodes  out  flop  support      residual  bound  tags
    tiny.dcert     48    2      1    1     0  all six ok   size 48       9  [tagState]
    seq.dcert      82    5      1    1     1  all six ok   size 149     19  [tagState]

    tiny  one cycle AGREES   trace 4 cycles AGREES
    seq   one cycle AGREES   trace 4 cycles AGREES

The only surviving `caseT` tag is `tagState`, which destructures the RUNTIME
state record — so Gate 0 holds for a certificate that arrived from a file, not
only for a Lean literal.

## Refusal paths — PASS

`wide.dcert` (a synthetic design exercising every v1 record shape) is refused,
and the per-field report says WHY rather than just "unsupported":

    support: wf true  memFree FALSE  sources FALSE  ops true  arities true
             flopClocks true
    asyncOK FALSE

Three independent reasons, reported separately, which is the point of reporting
six fields instead of one boolean.

## FINDING 1 — DCERT1 v1 cannot express `asyncReset`, and it bites on real data

`rt_btb_gate.dcert` (1782 nodes, 64 flops, memory-free) passes ALL SIX
`SupportedByProjection` fields and is still refused:

    support: wf true  memFree true  sources true  ops true  arities true
             flopClocks true
    asyncOK FALSE  ->  REFUSED

Its sources include `flopQAsync`, so the design has asynchronous resets; a v1
flop record has eight fields and none of them is `asyncReset`, so every flop
parses with `asyncReset = false`.  The checker on this branch ties the two
together (`asyncFlagMismatch`); the FORMAT cannot.  Running it would have
simulated a different design — a synchronous-reset one — and reported
agreement, which is exactly the failure mode a loader must not have.

`CertIO.asyncOK` detects it after parsing and `runOne` refuses.

This is a FORMAT gap, not a semantics gap.  Fixing it means DCERT1 v2 carrying
`clock` and `asyncReset` (and a clock table), which is exporter work.

## FINDING 2 — the specializer is the scale bottleneck, two ways

`rt_intpipe_alu.dcert` is the smallest real design that is fully in scope:
5118 nodes, 4045 sources, 2 outputs, NO flops, NO memories, sources `input` and
`const` only, all six support fields true, `asyncOK` true.

    parse 203 ms  (158 KB)      support check 0 ms
    mkSim  ->  deep recursion was detected at 'interpreter'
               10309 interpreter frames, alternating mixTerm / mixTerms

`--tstack` does NOT help: it raises the ELABORATION stack, and this overflow is
in the IR interpreter that `lean --run` uses.  Tried at 65536 and 262144, same
failure, 0.6 s each.

Separately, specialization time is SUPERLINEAR in node count.  Measured through
`Hw.mkSim (Scaling.chainD n)`, interpreted:

    n      mkSim          ratio
     256    14212 ms
     512   101480 ms      7.1x for 2x nodes   (~ n^2.8)
    1024   >600000 ms     did not finish in a 600 s budget
    2048   >600000 ms     did not finish in a 600 s budget

Each `n` ran in its own `lean --run` with `timeout 600`; 1024 and 2048 were
killed by that timeout, not by a stack overflow.  The fitted exponent predicts
101.5 s * 2^2.8 ~ 710 s for n = 1024, which is consistent with timing out at
600 s, so the two timeouts corroborate the exponent rather than adding a new
failure mode.

Phase 1 made the residual's SIZE linear and increment 1 measured its BOUND
linear (`3n + 5`).  Neither says anything about how long the specializer TAKES,
and this is the first measurement of that.  At n^2.8, 5118 nodes is ~3 orders of
magnitude beyond n = 512 even without the stack limit -- and n = 1024 already
exceeds ten minutes.

So there are two independent walls, and the stack one fires first:

  * stack: the interpreted `mixTerm`/`mixTerms` recursion is proportional to
    the residual's depth, which is linear in n.  A native build (`leanc`) would
    move this to the OS stack, where `ulimit -s` applies.
  * time: n^2.8 in the specializer itself, which no build mode fixes.

NOT attempted, deliberately: weakening the fragment checker, lowering the
specialization fuel, trimming the design, or reporting a partial run as a
success.

## What this increment does NOT claim

`stepOf_correct` is conditional on `SimWF`, which contains
`projectDesign D = .ok R` — a kernel equation the specializer does not reduce
(measured two commits ago).  Agreement observed by the runner is EXECUTION
EVIDENCE about one file.  A loaded certificate has no kernel proof, and nothing
in this increment pretends otherwise.
