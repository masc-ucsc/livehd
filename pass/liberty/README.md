# pass/liberty — Liberty cell behavioral models (task 2a-abc companion)

`lhd pass liberty gensim <file.lib> --emit-dir lg:models` reads a Liberty file
(via ABC's `read_lib`) and emits one **LGraph behavioral model per combinational
cell**. Each model is a `Graph` named exactly after the Mio cell, with input pins
= Mio pin names and the gate output pin; its function is lowered from the cell
SOP (`Mio_GateReadSop`) to `And/Or/Not` (or a constant for tie cells).

These models resolve the blackbox cell `Sub`s emitted by `pass abc` so LEC is
self-contained — no PDK Verilog dependency. Names and pins match the netlist
`Sub`s exactly because both go through the same `read_lib`-derived Mio library.

```
lhd pass liberty gensim file.lib --emit-dir lg:models
# then: cgen(lg:abc_netlist) + cgen(lg:models) is a complete Verilog design
```

Sequential cells are skipped (no parseable comb SOP) — flops stay native
`Flop` cells in the netlist, never mapped to library DFFs.
