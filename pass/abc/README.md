# pass/abc — ABC technology mapping (task 2a-abc)

`lhd pass abc --top <mod> lg:dir --emit-dir lg:netlist` technology-maps a
design to a standard-cell netlist. A prior coloring (`pass color synth`) is
**optional**: it controls how the design is split into per-region modules. With
no coloring (or for any node left at color 0), color 0 is treated as just
another color — the uncolored logic becomes a single `<mod>__c0` region — and
the pass emits one warning that it is partitioning an uncolored design.

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
   `lt/gt/eq` (via the selectable adder library, 2i-abc_arith), `mult` (a simple
   single-cycle array multiplier whose partial-product additions reuse the
   selectable adder; sign/zero-extended to the magnitude width then multiplied
   mod 2^W, matching the LEC for signed and unsigned alike), `shl` (a constant
   amount becomes pure bit re-wiring, a runtime amount a combinational barrel/log
   shifter; multi-driver one-hot amounts are ORed, matching the LEC), `sra` (right
   shift: arithmetic for a signed operand, logical otherwise — a constant amount
   re-wires, a runtime amount is a barrel shifter), and constants. `div` (and
   `mod`, which lowers to `a-(a/b)*b`, hence a `div`) is **blackboxed**: a
   synthesizable divider is large and out of scope, so the `div` node is kept
   native as a boundary (like a `Sub`/memory) and a `div-blackbox` warning fires.
   Anything else is still an `unsupported-cell` error.

   Magnitude-width note: `mult`/`sra` size their result at the LEC's `real_width`
   (LiveHD reserves the top bit of an *unsigned* net as an always-0 sign slot), so
   the spare bit is held at 0 rather than carrying a stray product/sign bit.
2. **flow** — `Abc_NtkToLogic` → run `pass.abc.flow` (comb and seq default
   `strash; &get -n; &fraig -x; &put; &get -n; &dch -f; &nf {D}; &put`)
   against the `read_lib -s` Liberty (`-s` skips multi-output cells — fa/ha
   supergates cannot be read back and previously collapsed their cone to
   const0 silently; the read-back now also hard-errors on any unreadable
   mapped node) → `Abc_NtkToNetlist`. Retiming (`dretime`) is deliberately
   NOT in the default seq flow (2opt-freq ruling): it reshapes the latch
   count/order, which drops register name preservation (post-synthesis LEC
   tier-1 correspondence, 3a-synth) and the din-cone source attribution, and
   it is a latency-visible transform the cycle-accurate loop gate forbids.
   Opt in explicitly via `flow` when that is understood.
3. **from ABC** — each mapped gate becomes a 1-bit blackbox `Sub` named after the
   Liberty cell (`Mio_GateReadName`), with pins from the Mio gate. Multi-bit
   outputs are reassembled with a `Set_mask` concat; inputs are bit-selected with
   `Get_mask`. PI/PO correspondence is matched by **creation order** (ABC
   preserves CI/CO order across the flow).

### Sequential (`seq=true`)

Registers cross into ABC as 1-bit **latches** (`Abc_NtkCreateLatch` + `Bi`/`Bo`)
so ABC can optimize the logic between them, but stay **native `Flop`s** on read-back
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

### Whole-design flatten (`flatten`)

The decomposition is per-def by default: every module reachable from `--top`
gets its own wrapper + `__c<color>` region modules, and child instances stay
blackbox boundaries. With `flatten` the instance hierarchy is structurally
inlined first (`pass/partition/flatten.cpp`: child bodies cloned per instance,
node/wire names prefixed with the dotted instance path, srcids/colors carried)
and ONE decomposition runs on the flat def. A single resulting region — the
`pass.color flat` case — is emitted directly under the top's own name and port
list: **one netlist module**, a drop-in replacement for the original def. All
cross-module `get_mask`/`set_mask` bus-packing glue disappears with the module
boundaries (only true blackboxes — memories with `memory=false`, div cones,
external IP — keep their PI/PO cuts), so `lhd pass opentimer` can time the
whole design in its default single-module mode. `auto` flattens exactly when
the active coloring came from `pass.color flat`; a multi-color coloring under
`flatten=true` still produces the wrapper, with regions spanning the former
hierarchy.

## Options (`--set pass.abc.<flag>=value`)

The option namespace matches the command path (`lhd pass abc`); after the
`pass abc` words the key may be abbreviated (`--set library=…`), see 2h-set_path.

| flag | meaning | default |
|------|---------|---------|
| `library` | Liberty `.lib` for `read_lib` | `$HAGENT_TECH_DIR/sky130_fd_sc_hd__tt_025C_1v80.lib` |
| `flow` | ABC command string (`{D}`/`{L}` substituted), run verbatim — see below | built-in comb/seq default |
| `seq` | sequential mapping (flops→latches, memories/Subs blackboxed) — a superset that also maps purely combinational regions, so it is the default | `true` |
| `adder` | comb adder architecture for `sum`/cmp (also the `mult` partial-product adds): `rca`/`cska`/`cla` | `rca` |
| `block_size` | CSKA/CLA block width (`0` = auto) | `0` |
| `multiplier` | comb multiplier architecture for `mult`: `array` (the only option today; the enum is the extension point for Booth/Wallace) | `array` |
| `delay` / `load` | `{D}` / `{L}` substitution: expands to the full flag (`-D <val>` / `-L <val>`) when set, to nothing when empty — `&nf {D}` needs `-D`, a bare value is silently ignored by ABC | empty |
| `verbose` | extra per-region prints (assume constraints, …) | `false` |
| `flatten` | whole-design flatten: `auto`/`true`/`false` — see below | `auto` |
| `qor` | write the QoR JSON (below) to this file | empty (`lhd pass abc` defaults it to `<workdir>/qor.json` under `--workdir`) |
| `region_opts` | per-region (color-keyed) overrides, JSON — see below | empty |

### Per-region overrides (`region_opts`, 2opt-freq C)

`flow`/`delay`/`load`/`adder`/`block_size`/`multiplier` can be overridden **per
color region**, so an agent can spend synthesis effort on the critical region
only:

```
--set pass.abc.region_opts='{"1":{"flow":"strash; resyn2; &get -n; &dch -f; &nf {D}; &put","delay":"2"},
                             "4":{"adder":"cla","block_size":4}}'
```

Keys are color ids (the region `<mod>__c<N>` suffix). Unset fields inherit the
global options. Two sources, later wins: a `"region_opts"` member embedded in
the source graph's `coloring_info` JSON (the Pyrope block-attribute channel,
2opt-freq B), then the CLI JSON above. Unknown option names, non-integer color
keys, or malformed values are **hard errors** — a mistyped hint never silently
no-ops. Each application is logged
(`region '…': color N options override applied (…)`); a region whose
overridden flow fails to produce a mapped netlist fails with the region's name
in the diagnostic while other regions still map.

> Known limitation: `adder=cla` with the auto block width on a wide `mult`
> produces an AIG that ABC's default `&dch` step aborts on (an ABC-internal
> failure that exits without a diagnostic). Use the default `adder=rca` (or
> `cska`, or a small `block_size`) for multiplier-heavy designs.

### The `flow` string and abc.rc scripts

`flow` is handed verbatim to ABC's `Cmd_CommandExecute`, one `;`-separated
command at a time. Both the classic AIG commands (`balance`, `rewrite`,
`refactor`, `resub`, `strash`, `fraig`, `dch`, `if`, `mfs`) and the GIA `&`-space
commands (`&get`/`&put`, `&dch`, `&fraig`, `&if`, `&nf`, `&deepsyn`, `&resub`,
`&mfs`) work as-is.

LiveHD drives ABC through the library entry (`Abc_Start`), which — unlike the
`abc` binary — never sources `abc.rc`, so its **named synthesis scripts are not
present by default**. The pass therefore installs the standard `abc.rc` scripts
as aliases at startup (`abc_map.cpp`, `kAbcAliases`), so a script name can be
used directly in `flow`:

- short building blocks: `b rw rwz rf rfz rs rsz st f dret`
- AIG opt scripts: `resyn resyn2 resyn2a resyn3 compress compress2 choice choice2`
- resub scripts: `resyn2rs compress2rs src_rw src_rs src_rws`
- GIA scripts: `&dc3 &dc4`

A custom `flow` replaces the whole built-in string, so it must still end with a
technology-mapping step (`&nf {D}`) for the read-back to find cells, e.g.
`--set pass.abc.flow="strash; resyn2; &get -n; &dch -f; &nf {D}; &put"`. Run
`lhd describe pass.abc.flow` for the in-tool cheat-sheet + the upstream `abc.rc`
link. Keep `kAbcAliases` and that help text in sync.

## QoR read-back (2opt-freq A)

After each region's flow, while ABC still holds the mapped *logic* network, the
pass reads back the region's **gates / Liberty area / critical delay**
(`Abc_NtkDelayTrace` over the Liberty pin-to-pin data — the same estimate ABC's
`print_stats` shows after mapping) plus the **worst-arrival region output**,
source-attributed to `file:line` through the output driver's `srcid`. One line
per region goes to the step log, a `pass.abc qor:` summary follows the region
loop, and with `qor=FILE` the whole thing is written as JSON:

```json
{"schema_version":1, "top":…, "library":…, "seq":…, "delay_target":…,
 "total":{"regions":N,"gates":G,"area":A,"max_delay":D,
          "critical_region":…, "critical_output":…, "critical_src":"file:line"},
 "regions":[{"module":…,"color":C,"gates":g,"area":a,"delay":d,
             "critical_output":…, "critical_src":…}, …]}
```

`lhd pass abc --workdir W` defaults `qor` to `W/qor.json` and embeds the file
as the result envelope's `"qor"` member, so an agent loop reads its score
straight from `--result-json`. **Per-region numbers only**: the delay is ABC's
mapped estimate inside one region — paths crossing region or blackbox
boundaries are invisible here (`pass.opentimer` is the whole-design scorer).
A region whose flow fails contributes no row; a `delay` below 0 (or absent in
the JSON) means the mapped network exposed no delay data.

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
hierarchy) and is the default (`seq=true`). The `mult`/`shl`/`sra` bit-blasts
(array multiplier, constant + runtime barrel shifters) are LEC-verified by the
arith fixture. `div` (and `mod`, which lowers through `div`) stays blackboxed.
QoR read-back (gates/area/delay + `qor.json`, above) is in. Not yet
implemented: per-region `flow` overrides (2opt-freq C). See
`todo/livehd/2opt-freq.html`.
