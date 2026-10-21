# Rule C and the gated-clock pair: what the window pins, and what it does not

Supersedes `LATCH_CONTRACT_HOTMUX_HOLD.md`, whose analysis was wrong in a way
worth recording. The first reading of this case was "every arm that reaches the
other latch is a Hotmux CONTROL, therefore the dependence is not real". That
does not follow. Controls select values; a control that moves inside the window
changes which data the latch passes, and dropping controls from the analysis
would hide exactly the hazard rule C exists for.

## The design

`minion_frontend_thread_buffer` instantiates `prim_clk_gate` and
`prim_rf_1r1w_diff_preview` with `PreviewClkSameAsRf = 1'b0`:

    el     : latch, open while clk == 0           -- the gate's enable latch
    gated  : clk & el.q                           -- the gated clock
    pv[7:0]: latch, open while gated == 0         -- the preview latch

Both resolve to the same commit net and edge, so rule C examines the pair, and
`el.q` does reach `pv`'s `din`. `pv` is 8 bits, so it lowers to eight 1-bit
latches whose `din` is `Sext(SRA(Hotmux(...), j), 1)`. The Hotmux is a VALUE
mux, not a hold mux -- `pv`'s window already lives on its `enable` pin -- and
its four controls are `Or`/`And` trees mixing `Ror(gated)` with data.

## The invariant that does hold

Over `pv`'s open window its enable root sits at a fixed level, by definition of
the window. So if every combinational path from `el` into `pv`'s `din` passes
through that root, `el` cannot move anything `pv` passes while `pv` is open,
and there is nothing to borrow.

Measured, by rerunning the cone walk with the window root pin treated as a
leaf: **`pv`'s whole `din` cone then reaches no latch at all.** Every route from
`el.q` into that data is through `gated`, which is `pv`'s own window root. The
earlier per-arm measurement could not see this -- four control sub-terms reach
`el` without being the clock term, and they reach it through gated-clock nodes
further in.

## What landed

`comb_reach` takes an optional `cut` pin, walked as a leaf. Rule C computes a
second reach set with the destination latch's window root cut, once per latch
and only when a pair would otherwise be refused, and skips the pair when the
other latch is absent from it.

Everything else stays in the analysis: every mux and hotmux arm, every control,
the don't-cares and the trailing default. Exactly one pin is pinned, and only
the pin the window argument justifies.

Five tests bracket it (`graph/latch_contract_test.cpp`), built on one fixture
that differs only in where `el.q` is allowed to reach:

| test | expectation |
|---|---|
| `GatedPreviewPairIsDischargedByItsOwnWindowRoot` | accepted |
| `HotmuxControlReadingTheOtherLatchIsRefused` | refused |
| `HotmuxValueArmReadingTheOtherLatchIsRefused` | refused |
| `HotmuxDefaultArmReadingTheOtherLatchIsRefused` | refused |
| `UngatedWindowDoesNotPinTheGatedClock` | refused |

The last one is the phase check itself: the destination latch's window is the
RAW clock, so pinning it leaves `clk & el.q` free in `el.q`. Mentioning the
clock is not the same as being pinned by the window.

Two mutants confirm the bracket. Removing the discharge fails only the accepted
case; discharging unconditionally fails all four counterexamples.

## What it does NOT buy

**The four designs are still not accepted, and the corpus stays at 112 of 122.**
With rule C discharged they get past `lhd compile` and stop at the next gate:

    pass.single_edge: gated-latch-open-when-off
    latch `...wr_data_en_1p_q[*]` has a gated clock window that stays
    transparent while the gate is off, which one commit edge cannot represent

That is a second, independent property of the same cell: `pv`'s window is
`!(clk & el.q)`, which is open for the whole time the gate is off, whatever the
clock does. All four modules report it -- `minion_frontend_thread_buffer`,
`minion_frontend`, `core_top`, `minion_top`.

So this change removes a false refusal and makes the real wall visible. It
credits no coverage, and it says nothing about RTL equivalence for these
designs.

## Gates run

The 531 latch / ICG / LEC / clock / single_edge targets: 530 pass.
`pass/lec:lec_mem_small_blast_test` hits its 15 s solver budget under parallel
load and passes on its own.
