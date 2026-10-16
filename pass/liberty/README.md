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

`lhd synth` and `lhd compile` also use explicitly selected `synth.liberty`
RAM cells as typed, opaque module interfaces. Scalar and bus pin directions,
widths and declaration order come from that library. The direct Slang reader
resolves named SRAM instances without `--ignore-unknown-modules`; unrelated
unknown modules remain errors. Mapping keeps those instances, with no generated
memory RTL, flop array, or fabricated functional model. Implicit truncation of
an SRAM input is refused rather than silently discarding address or data bits.

This is binding of an explicitly instantiated macro, not automatic substitution
of an inferred array based on dimensions. A Liberty `memory()` group and its
timing arcs do not establish read/write collision, forwarding, reset or
cross-port priority semantics. Such inference needs a compatible functional
contract and, where required, an external adapter. In particular, per-bank
LEF/Liberty cells require real bank decoding and read-bank selection; the macro
name alone does not establish its address width or capacity.

Clock-edge-only SRAM output timing arcs mark the corresponding opaque output as
a registered boundary in the graph declaration. Combinational or unspecified
outputs keep their dependency arcs; this does not create a functional RAM model.
The nested bit-range cycle check uses the same per-output boundary as port reach.

The memory inventory skips cells it cannot represent (a non-RAM `memory()`
type such as `rom`, an `inout` pin, an unresolved `bus_type`, a malformed
width) instead of failing every read of the library; instantiating such a cell
remains an unknown-module error. `internal` pins are not ports. A `bus_type`
resolves against cell-scope `type()` groups first, then the library's. A
reused `lg:` directory keeps a matching body-less RAM declaration and replaces
one left by an older `.lib`; a same-named module with a body is refused.
