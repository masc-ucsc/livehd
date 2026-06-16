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
lhd compile ...                         --emit-dir lg:orig
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
   `mux/hotmux`, `get_mask/set_mask/sext` (constant mask/position), `sum` +
   `lt/gt/eq` (via the selectable adder library, 2i-abc_arith), and constants.
   Still `unsupported-cell`: `mult/div/mod` and variable shifts.
2. **flow** — `Abc_NtkToLogic` → run `pass.abc.flow` (comb default
   `strash; &get -n; &fraig -x; &put; &get -n; &dch -f; &nf {D}; &put`; seq
   default adds `dretime`) against the `read_lib` Liberty → `Abc_NtkToNetlist`.
3. **from ABC** — each mapped gate becomes a 1-bit blackbox `Sub` named after the
   Liberty cell (`Mio_GateReadName`), with pins from the Mio gate. Multi-bit
   outputs are reassembled with a `Set_mask` concat; inputs are bit-selected with
   `Get_mask`. PI/PO correspondence is matched by **creation order** (ABC
   preserves CI/CO order across the flow).

### Sequential (`seq=true`)

Registers cross into ABC as 1-bit **latches** (`Abc_NtkCreateLatch` + `Bi`/`Bo`)
so ABC can retime/optimize across them, but stay **native `Flop`s** on read-back
(never mapped to library DFFs — the Liberty stays purely combinational). Each
flop's Q-net seeds `bitnet` as a source; its latch D is wired (after the comb
loop) to the folded next-state `reset? rval : (enable? din : Q)`, so the
reconstructed flop is a plain D-flop with only the region clock + power-on
`initial` reattached, and ABC sees the true register function. Latch init comes
from the reset/initial constant. On read-back, latches rebuild into native flops:
a single-root region (one register name) collapses to one named flop, a 1:1
multi-register region rebuilds one flop per register, and a retiming-reshaped
region falls back to `<region>__r<n>` 1-bit flops (all LEC-correct).

### Blackbox boundaries (memories + hierarchical `Sub`)

A region `Memory` or child `Sub` instance is never bit-blasted: its consumed
output pins become fresh ABC PIs (sources), its combinationally-driven inputs
become ABC POs (the cones feeding them), constant inputs are recreated directly,
and the node is rebuilt natively (a `Sub` is re-linked to the partitioned child
def) and reconnected. Boundary PIs/POs are appended after the region ports so the
region read-back stays index-aligned.

ABC's frame is global: one `Abc_Start`/`Abc_Stop` per run, `read_lib` once before
the region loop.

## Options (`--set pass.abc.<flag>=value`)

The option namespace matches the command path (`lhd pass abc`); after the
`pass abc` words the key may be abbreviated (`--set library=…`), see 2h-set_path.

| flag | meaning | default |
|------|---------|---------|
| `library` | Liberty `.lib` for `read_lib` | `$HAGENT_TECH_DIR/sky130_fd_sc_hd__tt_025C_1v80.lib` |
| `flow` | ABC command string (`{D}`/`{L}` substituted) | built-in comb/seq default |
| `seq` | sequential mapping (flops→latches, memories/Subs blackboxed) | `false` |
| `adder` | comb adder architecture for `sum`/cmp: `rca`/`cska`/`cla` | `rca` |
| `block_size` | CSKA/CLA block width (`0` = auto) | `0` |
| `delay` / `load` | `{D}` / `{L}` substitution | empty |
| `verbose` | per-region gate count | `false` |

## Source-map carry-through

ABC's `strash`/`dch` destroy per-node provenance, so after mapping each gate is
re-attributed to the **original output cone** it feeds: each output port's driver
`srcid` is re-minted into the body locator (`import_from`), and a backward DFS
from every PO stamps each gate `Sub` in that PO's fanin cone with the output's
srcid (a gate feeding several outputs gets a `combine(...)`, lowest output index
= primary). The output-concat `Set_mask` glue carries the port srcid too. The
emitted netlist therefore points back to the pre-ABC RTL (verify with `cgen --set
cgen.srcmap=1`). Attribution is per-cone, not per-gate — ABC's optimization is
lossy, so exact gate lineage is unrecoverable.

## Status

Combinational mapping (`seq=false`) is complete and LEC-verified
(`//lhd/tests:lhd_abc_test`), with source-map carry-through (above) and the
selectable `sum`/comparator bit-blast (`//lhd/tests:lhd_abc_arith_test`).
Sequential mapping (`seq=true`) — flops↔latches with name preservation +
single-root remap, and memory/`Sub` blackbox boundaries — is complete and
LEC-verified (`//lhd/tests:lhd_abc_seq_test`: flops, memory, and a 3-level
hierarchy). Not yet implemented: `mult/div/mod` + variable-shift bit-blast, and
per-region `flow` overrides. See `todo/livehd/2a-abc.html`.
