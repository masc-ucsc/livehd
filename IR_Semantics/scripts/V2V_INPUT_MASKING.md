# The v2v differential compared bits the design does not have

`IR_Semantics/scripts/v2v_verilator_diff.py` drives the original and the regenerated Verilog
with identical stimulus and diffs one line of outputs per cycle. It did two
things that Verilator does not define:

* it poked each input with a full 64-bit random word, whatever the port's
  declared width;
* it printed each output by reading the Verilator member raw.

Verilator makes no promise about the bits above a signal's width. Two models of
the same design are free to mask at different points, and they do. So the
harness could report a difference between two models that agree on every bit
the design has.

Both are fixed: writes and reads are masked to the declared width.

## What it cost

**`txfmaexp_top`'s "miscompile" was this.** `CENSUS_D2_122.tsv` carries it as
`blocked -- RTL differential mismatch (7/2980) under BOTH lowerings;
pre-existing, unresolved`, and it was the one module in the corpus held back by
a suspected compiler bug rather than a refusal.

Measured on master:

| run | result |
|---|---|
| unmasked writes, unmasked reads | 285 of 3000 cycles differ |
| unmasked writes, MASKED reads | still differs, first at cycle 6 |
| masked writes, masked reads | **match, 200,000 cycles, all 31 outputs** |

So it is the stimulus, not the comparison: feeding a port a value it cannot
hold is what made the two models disagree.

The localisation survives as a check on the above rather than as a bug. The
divergent outputs were exactly `txfma_e6`'s, and the cone behind them is
`txfma_e5`'s `exp_sticky1` CSA and adder -- which is where this project's
earlier note put it (`exp_hi_prod_csa` feeding `exp_hi_ep_adder`, RTL `0x374`
vs generated `0x174`, a lost bit 9). With the harness fixed:

* `txfma_e6` alone: match;
* `txfma_e5` alone: match;
* `txfma_e5` with six observers INSIDE the implicated cone -- the two sticky1
  input muxes, the CSA sum and carry, the adder's carry-in, and
  `e_sticky1_add_f5a_h` itself -- match on every cycle;
* `txfma_adder` and `txfma_csa` read against their generated forms: the
  generated adder is `sum = cin + b + a` into an 11-bit temporary, sliced to
  10, which is what the RTL says.

All 85 `txfmaexp_top` ports agree with the original in width and direction, so
the masking is not hiding a narrowed port -- that was checked rather than
assumed, because the harness takes its widths from the GENERATED file and a
narrowed port would have been masked away silently.

## What this does NOT say

* It is not a proof of equivalence. 200,000 random vectors is a bug finder that
  did not fire.
* The 7/2980 figure came from a DIFFSIM harness that is not on master, so this
  does not establish that THAT run was the same artifact. Its own control is
  the reason to suspect so: both lowerings agreed with each other and both
  differed from the RTL, which points at the RTL-side model rather than at the
  compiler.
* Nothing here changes the corpus count. `txfmaexp_top` already exports on
  master (`IR_Semantics/evidence/CENSUS_MASTER_122.tsv`).

## Reproducing

    IR_Semantics/scripts/v2v_verilator_diff.py --top txfmaexp_top \
      --orig <anchor.sv> --orig-flags "-F <coreet filelist>" \
      --gen <regenerated .v> --cycles 200000

Regenerate the `.v` with `lhd compile lg:<raw lgdb> --top <m> --emit
verilog:<out.v>`. To see the old behaviour, drop the mask from
`cpp_member_write`.
