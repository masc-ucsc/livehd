# Race: a latch that opens on the edge a flop samples at

Not an Icarus-vs-Verilator disagreement: the two simulators AGREE here, and
`lhd sim` differs from both on purpose. Kept for a decision.

`fz.v`: `lh` is transparent while `clock` is high, so it opens at the rising
edge; `q` is a posedge flop that reads `lh` at that same edge.

| cycle | a | Icarus / Verilator `o0` | lhd (slop, llvm) `o0` |
|-------|---|-------------------------|-----------------------|
| 1 | 6 | 6 | 5 |
| 2 | 7 | 7 | 6 |

Zero-delay Verilog leaves the order of the latch's `always @(*)` and the flop's
`always @(posedge clock)` open at that edge. Both simulators run the latch first,
so the flop captures the NEW input. Hardware (and lhd) has the flop capture the
value the latch HELD while it was closed (the latch output changes only after
the edge). yosys synthesizes it as a `$dlatch` feeding a `$dff`, i.e. the lhd
answer.

`fuzz_92000318_reduced.v` is the fuzz case that found it (a gated-clock flop
reading such a latch). The fuzzer no longer generates a flop that reads a
high-transparent latch.

    iverilog -g2012 -o tb.vvp tb.v fz.v && vvp -n tb.vvp
