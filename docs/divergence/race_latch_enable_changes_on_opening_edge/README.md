# Race: a latch enable that changes on the edge that opens the latch

Not an Icarus-vs-Verilator disagreement: the two simulators AGREE, and
`lhd sim` differs from both. Kept for a decision.

`fz.v`: `lh` is transparent while `clock && t1`, and `t1` is a posedge register,
so it changes on the very rising edge that opens `lh`.

| cycle | a | t1 after the edge | Icarus / Verilator `o0` | lhd (slop, llvm) `o0` |
|-------|---|-------------------|-------------------------|-----------------------|
| 1 | 6 | 0 | 6 | 5 |
| 3 | 8 | 0 | 8 | 7 |

At the edge both simulators evaluate `clock && t1` with the OLD `t1` (1) before
the nonblocking update lands, so the latch is open for a zero-width pulse and
captures `a`; then `t1` drops and it closes. lhd evaluates the open phase with
the post-edge `t1` only (a glitch-free gate). In silicon `clock & t1` glitches
here: whether the latch captures depends on clock-to-q vs. gate delay.

`fuzz_93000381_reduced.v` is the fuzz case that found it. The fuzzer no longer
lets a clock-high latch's enable read a register.

    iverilog -g2012 -o tb.vvp tb.v fz.v && vvp -n tb.vvp
