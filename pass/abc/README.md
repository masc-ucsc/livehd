# pass/abc — ABC technology mapping (task 2a-abc)

`lhd pass abc --top <mod> lg:dir --emit-dir lg:netlist` technology-maps a
**colored** design (run `pass color synth` first) to a standard-cell netlist.

It reuses `pass.partition`'s decomposition seam
(`Pass_partition::build_decomposition` + a body-builder hook): one module per
color region, with the **module structure identical to `pass partition`** so each
netlist module LEC-checks against its `partition` twin. The body-builder hook
replaces each region body with an ABC-mapped netlist instead of the original
logic.

## Flow

```
lhd elaborate ...                       --emit-dir lg:orig
lhd pass color synth --top m lg:orig
lhd pass abc         --top m lg:orig    --emit-dir lg:netlist   # this pass
lhd pass partition   --top m lg:orig    --emit-dir lg:restruct  # the LEC twin
lhd pass liberty gensim file.lib        --emit-dir lg:models    # cell behavior
# LEC: cgen(netlist)+cgen(models) ≡ cgen(restruct), per module
```

## How it maps (`abc_map.cpp`)

Per region (`Region_body` from the partition seam):

1. **to ABC** — bit-blast each comb cell into a 1-bit AIG netlist
   (`ABC_NTK_NETLIST`/`ABC_FUNC_AIG`). Multi-bit module IO becomes per-bit ABC
   PIs/POs (the bit-blast boundary). Supported cells: `and/or/xor/not`,
   `mux/hotmux`, `get_mask/set_mask/sext` (constant mask/position), and
   constants. Any other cell (arithmetic, `flop`, `memory`, `sub`) is a loud
   `unsupported-cell` diagnostic.
2. **flow** — `Abc_NtkToLogic` → run `pass.abc.flow` (default
   `strash; &get -n; &fraig -x; &put; &get -n; &dch -f; &nf {D}; &put`) against
   the `read_lib` Liberty → `Abc_NtkToNetlist`.
3. **from ABC** — each mapped gate becomes a 1-bit blackbox `Sub` named after the
   Liberty cell (`Mio_GateReadName`), with pins from the Mio gate. Multi-bit
   outputs are reassembled with a `Set_mask` concat; inputs are bit-selected with
   `Get_mask`. PI/PO correspondence is matched by **creation order** (ABC
   preserves CI/CO order across the flow).

ABC's frame is global: one `Abc_Start`/`Abc_Stop` per run, `read_lib` once before
the region loop.

## Options (`--set abc.<flag>=value`)

| flag | meaning | default |
|------|---------|---------|
| `library` | Liberty `.lib` for `read_lib` | `$HAGENT_TECH_DIR/sky130_fd_sc_hd__tt_025C_1v80.lib` |
| `flow` | ABC command string (`{D}`/`{L}` substituted) | built-in comb default |
| `seq` | sequential mapping | `false` (`true` not implemented yet) |
| `delay` / `load` | `{D}` / `{L}` substitution | empty |
| `verbose` | per-region gate count | `false` |

## Status

Combinational mapping (`seq=false`) is complete and LEC-verified
(`//lhd/tests:lhd_abc_test`). Not yet implemented: `seq=true` (flops→latches +
name preservation), arithmetic-cell bit-blast (`sum/mult/cmp/shift`), and srcid
carry-through. See `todo/livehd/2a-abc.html`.
