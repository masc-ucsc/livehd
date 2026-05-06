# Pyrope/Verilog equivalence examples

Each pair `<name>.prp` and `<name>.v` describes the same module. These are
intended as fixtures for a future flow:

```text
prp -> lnast -> upass -> lgraph -> cgen verilog
inou/yosys/lgcheck --reference <name>.v --implementation <cgen-output>.v --top <name>
```

Current coverage:

- `trivial`: 1-bit xor smoke test.
- `arith_logic`: arithmetic, bitwise ops, shifts, and result constraints.
- `bit_slice`: bit and range selection.
- `compare_bool`: integer comparisons and boolean outputs.
- `mux_ifelse`: simple if/else mux lowering.
- `mux_priority`: sequential conditional assignment, last write wins.
- `uif_select`: `unique if` with mutually exclusive conditions.
- `wrap_sat`: supported `wrap` and `sat` assignment semantics.
- `reg_enable`: scalar `reg` lowering to flop with enable/reset.
- `memory_rw`: persistent indexed `reg` array lowering to memory with
  forwarding on same-cycle read/write.
