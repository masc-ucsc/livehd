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

ABC's `read_lib` drops sequential cells, so the ones `pass abc` can map onto
are read from the Liberty text by `liberty_dff.cpp` (`resolve_dff_cells`) and
modeled from their `ff` / `latch` / clock-gate groups: the plain DFF pick and
its drive ladder (`Flop`, `Flop(Not(D))` for a QN cell), the asynchronous
clear/preset flops (`Flop` with an async `reset_pin`), the integrated
clock-gate cells (`CLK & Latch(!CLK, EN|SE)`), and the transparent data-latch
cells (`Latch(din=D, enable=CLK)`, through `Not` for an active-low enable or a
QN output, with the Latch's own `reset_pin` for a clear/preset latch). Every
other sequential cell is skipped.
