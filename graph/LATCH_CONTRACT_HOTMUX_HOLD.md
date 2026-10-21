# Rule C refuses a hold mux it cannot recognise: the `Hotmux` spelling

**Status: open defect, not fixed here.** This records what it is, the evidence,
and the three ways out, because closing it changes when a safety gate accepts a
design and that is a decision rather than a cleanup.

## What happens

Four of CORE-ET's 122 modules fail `lhd compile` on the latch contract, all with
the same rule-C report and all naming the same cell pair:

| module | the pair, as the diagnostic prints it |
|---|---|
| `minion_frontend_thread_buffer` | `u_clk_gate.en_latch` / `u_inst_data.wr_data_en_1p_q[*]` |
| `minion_frontend` | `gen_thread_buf[0].u_tb.` + the same two |
| `core_top` | `u_frontend.gen_thread_buf[0].u_tb.` + the same two |
| `minion_top` | `u_core.u_frontend.gen_thread_buf[0].u_tb.` + the same two |

> latches `…u_clk_gate.en_latch` and `…u_inst_data.wr_data_en_1p_q[0]` are
> transparent at the SAME time and there is a combinational path between them

The other three instantiate the first, so this is one defect counted four times.
It is 4 of the 10 modules the corpus does not accept.

## The design

`minion_frontend_thread_buffer` instantiates `prim_clk_gate` (whose
`en_latch` is transparent while `clk_i` is low) and `prim_rf_1r1w_diff_preview`
with `PreviewClkSameAsRf = 1'b0`, so the preview side runs on the gated clock:

```systemverilog
always_latch begin
  if (!preview_clk_i) begin
    wr_data_en_1p_q = wr_data_en_1p_next_i;
  end
end
```

Both latches really are open together — `clock_gated` is low whenever `clk_i`
is — and `en_latch` really does reach `wr_data_en_1p_q`, through the gated clock.
Rule C is right about the facts. It is wrong about what they mean, because the
path it found is the latch's CLOCK, not a value the latch passes, and the clock
relation is what `commit_class_of` already models.

`latch_contract.cpp`'s own rule-B comment records this exact situation being
fixed once:

> Walked raw, the selector of a preview latch on a gated clock
> (`Ror(clk & el) ? 0 : nxt`) reached the gate's enable latch `el`, which shares
> its commit edge, and rule C refused CORE-ET's `prim_clk_gate` + preview
> register file at compile.

The fix was to peel the hold mux. It covers the two-arm `Ntype_op::Mux`
spelling. This instance does not arrive that way.

## Why the peel misses it

Here `WWidth/RWidth` is `256/32`, so `wr_data_en_1p_q` is 8 bits wide and lowers
to eight 1-bit `Latch` nodes. Each bit's `din` is `Sext(Hotmux(…), 1)` — bit 7
through an `SRA` as well — and the hold mux is a **`Hotmux`**, the interleaved
`(control, value)` form, not a `Mux`.

Both peels terminate only on `Ntype_op::Mux`:

* `latch_transparent_arm` walks `Get_mask`/`Sext` wrappers and then tests for a
  `Mux`; a `Hotmux` falls through and it returns `din` unchanged;
* `latch_hold_path` walks `Get_mask`/`Sext`/`SRA` and also returns only on a
  `Mux`, so it yields nothing.

`d_open` is therefore the raw `din`, and the cone walk enters the selectors.

## The measurement that settles it

An instrumented build reported, for each pin of that `Hotmux`, whether its cone
reaches any other latch. The pin layout is `cell.hpp`'s documented interleaving:
even pids are the one-bit controls, odd pids the values, with a trailing default.

| pid | kind | driver | reaches `en_latch`? |
|---|---|---|---|
| 0 | control | `or` | **yes** |
| 1 | value | const | no |
| 2 | control | `and` | **yes** |
| 3 | value | const | no |
| 4 | control | `and` | **yes** |
| 5 | value | don't-care graph input | no |
| 6 | control | `and` | **yes** |
| 7 | value | `Sext`, 8 bits | no |
| 8 | default value | `Sext`, 8 bits | no |

Every arm that reaches `en_latch` is a CONTROL. No value arm reaches any latch
at all. So no value this latch passes depends on the gate's enable latch, and
the refusal is entirely selector-driven.

The same probe confirms the asymmetry: `en_latch` itself peels correctly
(`arm=peeled`), and only the preview latch's bits come through as `arm==din`.

## Ways out

1. **Teach both peels the `Hotmux` hold spelling.** Structurally the right
   place, and it matches how `Mux` is handled. It needs `classify_mux_arms`'s
   proof obligation restated for N arms: the hold evidence here is a mix of
   don't-care constants and a don't-care graph input across several value pins,
   not a single Q feedback arm. Getting that wrong ACCEPTS a design rule C
   should refuse, which is the unsound direction.
2. **Stop rules B and C from walking a hold mux's CONTROL pins.** Narrower, and
   it is the principle the existing peel already encodes: rule C exists for
   logic straddling between DATA, and the clock relation is `commit_class_of`'s
   job. It fixes this case without classifying arms.
3. **Leave it.** Then these four modules stay out, and the reason is recorded
   rather than rediscovered.

## Reproducing

Build `//lhd:lhd`, then run the module through the CORE-ET pipeline with
`COREET_TOP=minion_frontend_thread_buffer` and `STOP_AFTER=compile`
(`scripts/run_coreet_module_lean.sh`). It stops at `pass.latch` in about a
second. The per-arm reachability above came from a temporary probe around the
`comb_reach(d_open, l, hit)` call in `graph/latch_contract.cpp`; it is not in
the tree.
