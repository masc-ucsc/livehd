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

   Width note: `mult`/`sra` size their result at the LEC's literal
   `real_width`; unsigned widths contain no hidden sign slot.
2. **flow** — `Abc_NtkToLogic` → run `pass.abc.flow` (comb and seq default
   `strash; &get -n; &fraig -x; &put; dc2; strash; &get -n; &dch -f; &nf {D}; &put -o`,
   plus the `buffer -N {F}; dnsize {B}` fanout/sizing tail, `abc_map.cpp`
   `kCombFlow`/`kSeqFlow`) against the `read_lib -s` Liberty — its **unit-delay
   GENLIB**: `&nf` minimizes logic depth on the smallest cells and every
   physical decision belongs to the SCL sizing steps and the budget ladder
   below ("A budget is a budget in BOTH directions"). `&fraig -x; &put; dc2`
   ahead of the `&dch -f; &nf` map is worth its runtime (dino: −4.4% gates,
   STA 51.1 → 44.4 ns); the final `&put -o` hands the mapped network back with
   a gate that drives several outputs decoupled by a *buffer* instead of a
   *duplicate* of the gate (see the identity-buffer note under step 3); an
   explicit `flow` replaces the whole default. With a `delay` target a region
   whose delay flow met its budget is mapped a second time for area
   (`kAreaFlow`: `… dc2; strash; dch -f; amap` + `buffer -N {F}; upsize {B};
   dnsize {B}`) and the smaller netlist that still meets the budget is kept. `-s` skips multi-output cells — fa/ha
   supergates cannot be read back and previously collapsed their cone to
   const0 silently; the read-back now also hard-errors on any unreadable
   mapped node. The result then passes through `Abc_NtkToNetlist`. Retiming
   (`dretime`) is deliberately
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

   **Identity buffers are aliased away, not read back.** ABC "decouples" every
   CO driver when `&put` rebuilds the logic network and again in
   `Abc_NtkToNetlist` (`Abc_NtkLogicMakeSimpleCos`): a CI that drives a CO gets
   a Liberty buffer, and a gate that drives two or more COs gets a duplicate of
   itself — or, with the built-in flows' `&put -o`, a buffer. Every PI→PO,
   flop-Q→PO, PI→flop-D and blackbox-boundary feed-through bit therefore came
   back as a real cell: on the lhdtrack corpus (asap7, `syn_lhd_verilog`)
   20,905 buffers = 4.1% of all cell area against yosys's 4, and 512 of
   `br_demux_onehot`'s 528 cells (31.26 vs yosys 1.40 µm²) were exactly that.
   Yosys never pays it because its ABC sub-netlist is built per signal, so no
   signal is both PI and PO and no CO shares a driver. The read-back therefore
   treats a single-input non-inverting gate (Mio truth `0xAA..`, the library
   buffer test) whose output net feeds **only COs** as a wire
   (`is_identity_gate`/`only_co_fanouts`, pass 1b): its output net is aliased to
   its input net, no `Sub` is minted, and `get_net_driver` follows the alias
   lazily so a latch-Q driver that only exists after pass 1c still resolves.
   `buffer -N` fanout buffers always feed at least one node, so they are never
   touched (they matter: `max_fanout=0` took `br_amba_axi_shrinker` from 292
   to 1146 ps). `gates`/`area` count only the cells minted (the incremental
   cache stores that row), and the per-region/total QoR JSON carries the
   `bypassed` count. The region `delay` still includes one buffer on a
   feed-through path — pessimistic by one buffer delay only when that path is
   the region's critical one. `lhd_abc_test.sh` pins the contract on a
   PI→PO / PI→flop-D / flop-Q→PO / shared-gate→2 PO fixture (no `BUFx1`,
   `gates` == the one real cell).

### Sequential (`seq=true`)

Registers cross into ABC as 1-bit **latches** (`Abc_NtkCreateLatch` +
`Bi`/`Bo`) so ABC can optimize the logic between them. Each flop's Q-net seeds
`bitnet` as a source; its latch D is wired (after the comb loop) to the folded
next-state `reset? rval : (enable? din : Q)` — a **synchronous reset** is a
D-cone mux with priority over the enable, exactly cgen's
`if (rst) q <= rval; else if (en) q <= din;` and pass/lec's
`ITE(rst, init, ITE(en, din, q))`, so it crosses like any other next-state
logic and its register maps to a plain DFF cell under the register name
(`r`, `r_<bit>`), with the `initial` dropped: it is the reset value, realized
on D, not a power-on value (cvc5 and lgyosys both prove the folded netlist
against the `reset_pin`+`initial` source; a plain DFF powers on X exactly like
the source register's own cgen). Keeping those registers native cost br_delay's
Pyrope flow 32 native flops that yosys's normalize then mapped to DFFHQNx1 +
64 INVx1 + 24 extra HB1 (18.196 vs 17.729 um^2, 114.5 vs 102.8 ps on ASAP7),
and left every reset-cone node native with fanout 77-113 (br_amba_axi_demux
2045 ps). Only an **asynchronous-reset** register (`async` pin asserted: tolg's
`sync=false` / `reset_style=async`, slang's `posedge clk or posedge rst`) stays
a native boundary (`reset-native` diagnostic), because the selected plain DFF
has no reset pin and folding the event into D would make it land only on a
clock edge; its surrounding data logic is still mapped. Latch init comes from a
resetless power-on constant only (a reset-backed latch is a don't-care to ABC:
the cell it maps to powers on X). On read-back, latches rebuild into native
flops or plain Liberty DFF cells according to the `register` option:
a single-root region (one register name) collapses to one named flop, a 1:1
multi-register region rebuilds one flop per register, and a retiming-reshaped
region falls back to `<region>__r<n>` 1-bit flops (all LEC-correct).

**DFF cell choice** (`pass/liberty/liberty_dff.cpp`): the smallest-area plain
posedge D-flop in the Liberty — an `ff` group with a bare posedge `clocked_on`
(a `!CLK` cell is negedge and never qualifies), a `next_state` that is one pin
or its complement (`D`, `!D`, `D'`), no clear/preset, one D and one CLK input,
and a Q output (a Q/Q_N cell uses Q) or, failing that, a QN output. Ties break
on fewest outputs, then non-inverted Q, then name. On ASAP7 that is
**DFFHQNx1** (0.2916 um^2; its only output is QN and its `next_state` is `!D`,
i.e. QN(t+1) = !D(t)) over the Q-only DFFHQx4 (0.3645); on sky130 dfxtp_1.
`dff_cell=<name>` forces one cell.

For a **QN cell** exactly one inversion per register must live in the mapped
logic, and the read-back always wires the cell's QN pin as the register's Q,
so it lands on the D side. Under the built-in flow the latch crosses ABC as
`BI <- ~next_state`: `&nf` folds the complement into its own phase assignment
(NAND/NOR/AOI/OAI/XNOR are costed in both phases) and mints an INV only where
nothing absorbs it (a D fed straight by a port — still cheaper than the
feed-through buffer it replaces). It perturbs the mapping either way (same
binary, asap7: br_credit_sender comb 67.0 -> 60.2 um^2, br_arb_rr 14.7 -> 18.0)
but costs only +52 um^2 of comb over the 10-test set against ~560 um^2 of flop
savings. That encoding is exact only under combinational transformations (the
machine ABC sees is `BO' = ~F(BO,x)`), so it is gated on the built-in flow; a
user `flow` (or a `small_flow`/`large_flow` tier), which may retime, keeps the
AIG honest and the read-back absorbs the inversion locally: a mapped root
inverter feeding only that latch is dropped, any other single-fanout root gate
is swapped for the cheapest Liberty cell computing its complement over the same
pins (`twin_index_`: AND2x2 -> NAND2xp33, AOI21xp33 -> AO21x1) when that beats
an inverter, and what remains gets one min-size inverter `<reg>__dinv` on D
(fanout 1). That path is exact under any flow but costs ~0.03 um^2 per flop on
ASAP7, which is why it is the fallback. yosys's dfflibmap keeps a Q-side INV
instead, which survives on ~40% of ASAP7 flops. A reshaped latch count under
the AIG-side encoding is a fatal internal diagnostic (unreachable: the built-in
flow never retimes). A register that keeps a resetless power-on init is rebuilt
native and never takes either path. `pass.liberty gensim` models a QN cell as
`Flop(Not(D))` -- the model's state cell holds the QN pin's value, because
pass/lec shares that state's power-on symbol with the source register (or
memory entry) it is named after; `Not(Flop(D))` shared the complement and
refuted every resetless register read before its first write on ASAP7 only.

**Drive ladder**: ABC's `buffer -N` tail never buffers a latch output (a CI),
so a register's Q fanout is uncapped and the read-back sizes the cell itself
from the same-shaped Liberty siblings sorted by area (ASAP7 DFFHQNx1/x2/x3):
Q-net fanout <= 8 -> x1 (the fastest rung there per the NLDM tables, and 97% of
registers), <= 16 -> x2, above -> x3 — measured br_amba_axi2axil 542 -> 637 ps
on x1 alone, 574 with the ladder. `abc.json` reports the pick and the per-rung
instance histogram under `"dff"`; the incremental cache is salted with the
resolved cell (`name:d:clk:q:inverted`), and `pass/liberty/liberty_dff.*` is
part of the code salt.

### Memory bit-blast (`memory=true`, `mem_lower.cpp`)

`memory=true` (the default) lowers every `Memory` cell IN PLACE — before
partitioning, so the result maps like any other flop+comb logic — into one
`bits`-wide native flop per entry (`<mem>__mem<i>`, power-on init from the cell's
`init` pin) plus:

- **write next-state** per entry: the write ports folded in ascending order
  (the highest-numbered enabled port wins a same-address collision, as cgen /
  cgen_sim / lec order it) with ONE `masksize`-wide `Mux` per write-enable lane
  (`bits/wensize`; a single bits-wide mux when `wensize==1`), never a per-bit
  chain. A port with a **constant address** is folded into its entry only — no
  `EQ`, no mux anywhere else; out of range it is dropped (what cgen's inline
  array emission does).
- **read ports**: a runtime address is a `Hotmux` over the one-hot decode
  (measured a wash against a binary tree once mapped, both become AND-OR
  covers); a constant address is a plain wire onto that entry's Q (out of
  range: 0, what the Hotmux yields when no arm hits). A non-constant read
  enable gates the data to 0 (cgen: x). Forwarding follows the per-(read,write)
  `fwd` matrix with the same per-lane muxes; a const/const address pair is
  decided at build time (unequal never collides, equal collides whenever
  enabled). `type==1` adds the one read-latency register (`<mem>__rdlat<port>`).
- **`read_all`** (the reserved whole-array driver): the `Concat` of the entry
  flops, entry 0 in the low bits — the `init`/`update` layout of
  `graph/cell.cpp`, cgen's `assign ra = <array>` and the lec encoder's
  `CONCAT(SELECT(a_cur, i))` — reading the COMMITTED contents (cgen refuses a
  read_all memory with a non-zero fwd/undef matrix, so no forwarding applies).

Why this shape: the previous fold built `EQ + bits x (getbit + and2 + mux)`
per (entry, port) — 97 nodes per 32-bit pair, 122k ABC input nodes for one
32x32 tile — and fed all-constant `EQ`s to the mapper for constant-address
ports (the bedrock multi-write tiles drive `wr_addr_k = k`); with the EQ width
bug those compares selected every same-parity entry (23,107 cells / 2,385 um2
/ 5.7 ns on `br_fifo_shared_dynamic_flops`, ASAP7 @400 ps, against yosys+abc's
8,472 / 1,002 um2 / 2.34 ns). The EQ fix alone brought that to 9,934 cells /
1,222 um2 / 0.84 ns (ABC folded the constant chains away); this fold then cuts
what ABC is handed from 122,239 to 3,723 input nodes (pre-strash AIG 187k ->
31k) for the same mapped result (9,905 cells / 1,222 um2, `pass abc` 3.4 -> 2.4
s), and lets the `read_all` memory of `br_tracker_linked_list_ctrl` bit-blast
(sky130 7,736 -> 7,396 um2).

What stays a native instance (a `memory-unlowered` warning names the memory):
whole-array cells (`update`/`reset` bus), `type==2` arrays, negedge clocks,
non-uniform write masks, a ROM (init contents and no write port: cgen emits a
flop init only under a reset, so the data would power on as X), and — as a
one-line `memory-max-bits` note — any memory whose `bits x size` exceeds
`memory_max_bits` (default 65536: one DFF per bit is the wrong realization of an
SRAM-class array). A memory with `ordering="none"` is bit-blasted but its
undefined collision window is REFINED to the committed value
(`memory-undef-refined`: sound only with the netlist as the lec IMPL side).
`memory=false` keeps every memory native (below).

### Blackbox boundaries (memories + hierarchical `Sub`)

A region `Memory` left native (`memory=false`, or one of the shapes above) or a
child `Sub` instance is never bit-blasted: its consumed
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
| `register` | map flops to Liberty DFF cells (`true`; falls back to native flops when the library has none) vs keep them native `always @(posedge)` (`false`) | `true` |
| `register_max_bits` | with `register=true`, keep a region's flops native when their total Q width exceeds this many bits (`register-kept-native` diagnostic; `0` disables — the default, since a bit-blasted 64x64 memory alone is 4096 bits and a native register is one the downstream normalize maps instead of pass.abc) | `0` |
| `dff_cell` | explicit Liberty DFF cell for `register=true` (empty = the smallest-area plain posedge D-flop, QN cells included; an explicit name also disables the drive ladder) | `` |
| `memory` | bit-blast a `Memory` into a DFF array + per-lane write muxes / read muxes (`true`, see above) vs keep it a native `cgen_memory_*` boundary instance (`false`) | `true` |
| `memory_max_bits` | with `memory=true`, keep a memory whose `bits x size` exceeds this many bits native, with a one-line note naming it (`0` disables) | `65536` |
| `adder` | comb adder architecture for `sum`/cmp (also the `mult` partial-product adds): `rca`/`cska`/`cla` | `rca` |
| `block_size` | CSKA/CLA block width (`0` = auto) | `0` |
| `multiplier` | comb multiplier architecture for `mult`: `array` (the only option today; the enum is the extension point for Booth/Wallace) | `array` |
| `delay` / `load` | the timing BUDGET in ps / the load: `{D}` / `{L}` expand to the full flag (`-D <val>` / `-L <val>`) when set, to nothing when empty — `&nf {D}` needs `-D`, a bare value is silently ignored by ABC. `delay` is also the target the built-in objective sizes to and judges the area candidate against (see below); `{B}` is the per-region budget (`delay` minus `reg_margin` when the region holds flops) as `-D <ps>` | empty |
| `area_relax` | max percent of a MET delay budget to trade back for area, via ABC's `&nf -R` (bounded by the real slack too); `0` disables that remap — see below | `200` |
| `area_flow` | the AREA candidate's ABC command string: empty = the built-in `strash; &get -n; &fraig -x; &put; dc2; strash; dch -f; amap` + `buffer -N {F}; upsize {B}; dnsize {B}`; `none` disables the candidate; anything else runs verbatim (`{D}`/`{L}`/`{F}`/`{B}` substituted, no tail appended) — see below | empty |
| `reg_margin` | register overhead subtracted from `delay` to form a flop-bearing region's budget: `auto` = the mapped DFF cell's clk→Q + setup read off its Liberty timing tables (ASAP7 DFFHQNx1 83.9 ps, sky130 dfxtp_1 528 ps), a number = that many ps, `0` = no margin — see below | `auto` |
| `verbose` | extra per-region prints (assume constraints, …) | `false` |
| `flatten` | whole-design flatten: `auto`/`true`/`false` — see below | `auto` |
| `qor` | write the QoR JSON (below) to this file | empty (`lhd pass abc` defaults it to `<workdir>/qor.json` under `--workdir`) |
| `region_opts` | per-region (color-keyed) overrides, JSON — see below | empty |

When a delay target is requested (`delay`, or a `region_opts` `delay` supplied
through `--set`) and the Liberty contains 2-D NLDM slew/load tables, the SCL
steps may run: the `buffer -N {F}; dnsize {B}` tail, the `upsize`/`dnsize`
steps of the budget ladder below, and the `stime`-shaped SCL timer that
reports the regional delay/area in picoseconds. The **mapper itself stays on
`read_lib -s`'s unit-delay GENLIB** (every pin 1.00): `&nf` produces a
min-depth mapping on the smallest cells and the sizing steps own every
physical decision. `pass.abc` used to replace that GENLIB with one derived
from the NLDM at ABC's gain-100 operating point whenever a delay was set; it
made `&nf` chase a delay it had no budget for, and measured over the 15
lhdtrack designs below it cost 1.50× yosys's area on ASAP7 (unit delay +
sizing: 1.07×) and 1.04× on sky130 (0.96×). `amap`, the area candidate's
mapper, ignores GENLIB delays altogether (bit-identical netlists either way).
Libraries containing only scalar delays keep the unbuffered, unsized
logic-depth mapping and produce a note.

Two caveats, both about `{D}` itself:

- The numbers are **picoseconds regardless of the Liberty's `time_unit`** — ABC
  normalizes every SCL library to ps/ff on read (`Abc_SclLibNormalize`), so a
  `time_unit : "1ns"` Liberty still reports and consumes ps here.
- The pinned ABC revision's `&nf` **parses `-D` but never reads it**
  (`giaNf.c` only consults `MapDelayTarget`, which `-D` does not set), so `{D}`
  alone selects the QoR units, not a mapper constraint. The only relaxation
  knob `&nf` honours is `-R <pct>`, a percentage of the mapper's OWN achieved
  delay (logic depth here) — which is what `area_relax` drives (below). A
  per-output required time would still have to come from ABC's
  `read_constr`/`Scl_Con` channel.

### A budget is a budget in BOTH directions (`reg_margin`, `area_relax`, `area_flow`)

Because `&nf -D` is inert, the built-in flow used to map every region for
**minimum delay** and then keep it, no matter how much timing slack the target
left. That is invisible under a tight ASAP7 constraint and enormous under a
relaxed sky130 one, where a mapped region beats its clock by 6–380× and pays
area for speed nothing asked for.

**The budget.** ABC's SCL timer sees one region's combinational cone; the
period OpenSTA checks also pays the launch flop's clk→Q and the capture flop's
setup. Measured on the lhdtrack netlists (OpenSTA minus the SCL comb delay):
75–87 ps on ASAP7 `br_arb_rr`/`br_counter_incr` (DFFHQNx1), 250–440 ps on
sky130 (dfxtp_1). So a region that holds flops gets the budget `delay −
reg_margin`, where `reg_margin=auto` reads the mapped DFF cell's clk→Q
(`cell_rise`/`cell_fall` of the CLK `rising_edge` arc at zero clock slew and
the middle load) plus setup (`setup_rising` `rise/fall_constraint`, table
center) off its Liberty timing tables, scaled by `time_unit` to ps
(`liberty_dff.cpp`): 73.0 + 10.9 = 83.9 ps for DFFHQNx1, 328 + 200 = 528 ps
for dfxtp_1, 0 for a Liberty whose flops carry no tables. The budget is
spelled into `{B}` (`-D <ps>`, floored to whole ps because ABC's `-D` is an
integer) before any of the region's flow strings are resolved, so the tails
size to it and the incremental recipe carries it.

So after the flow, `pass.abc` times the mapped network with the SCL timer and
walks a ladder, cheapest step first, each only while the previous still
misses the budget:

1. the flow's own `buffer -N; dnsize -D <budget>` (already run);
2. `upsize -D <budget>; dnsize -D <budget>` — `upsize -D` stops as soon as the
   SCL delay is inside the budget, the down-size recovers around it;
3. `upsize; dnsize`, the UNBOUNDED speed-grade sweep: `upsize` without a target
   chases the fastest cell assignment and `dnsize` then preserves that delay.
   Only for a real miss.

Under budget (steps 1–2 met it) the measured slack is handed back as
`&nf -R <pct>`, capped by `area_relax` and by the slack itself, and the mapper
is re-run: `&undo` restores the GIA `&nf` consumed, so `&fraig`/`dc2`/`&dch` —
the expensive part — run once. `-R` relaxes the mapper's depth model while the
slack came from the SCL timer, so a miss after the remap is repaired with step
2. `&undo` reverses exactly ONE step, which is why there is no second undo.

**The area candidate.** A region that met its budget is then mapped a second
time from the same pre-flow logic network (`Abc_NtkDup` before the frame took
it) with `area_flow` — `dch -f; amap` + `buffer -N; upsize -D; dnsize -D`
(`amap` maps for area with structural choices; its min-size cells rarely meet
a tight target on their own, hence the `upsize` first) — timed by the same SCL
timer, and the netlist with the smaller SCL area **among those that meet the
budget** is kept; the delay flow wins a tie and every region where the
candidate could not qualify (no target, a custom or size-tier `flow`, the
dummy-PO sentinel, `area_flow=none`, `input_ge` over `large_ge`). Both are
complete mapped logic networks with the latches untouched (amap maps the logic
between them; the QN encoding's `~f` and the identity-buffer bypass work
unchanged — flop counts and LEC verified), so the read-back does not care
which one won. The QoR row records the decision (`budget`, `candidate`,
`delay_flow`/`area_flow` SCL pairs).

Measured over 15 ../lhdtrack designs plus `br_amba_axi_demux` (geomean vs
yosys+abc, area / OpenSTA delay, designs meeting their period), the tree
before this objective vs after:

- **ASAP7**: area 1.50 → **1.07** (−29%), delay 0.81 → 0.88, met 7 → 5 of 16.
  `mul` 42.1 → 26.0 µm², `br_enc_gray2bin` 36.9 → 17.6, `br_ecc_sed_decoder`
  18.2 → 4.6, `br_ecc_secded_encoder` 24.9 → 11.1, `br_arb_rr` 22.6 → 16.4
  (436 → 395 ps, now meets 400), `br_amba_axi_demux` 437 → 382. The two
  periods lost are `icmp` (100 ps target: 74 → 102 ps at 5.5 → 3.4 µm²) and
  `br_enc_gray2bin` (200: 171 → 239 at half the area) plus `secded` (100:
  98.5 → 120): on those a min-depth mapping plus the unbounded `upsize` sweep
  cannot buy back what the physical-delay mapper's cell choices gave, and
  `br_amba_axi2axil` (550 → 633 ps at 222 → 218 µm²) is the same effect on a
  design that misses either way.
- **sky130**: area 1.04 → **0.96** (−7%), delay 0.83 → 0.83, 16/16 inside the
  20 ns period. `br_enc_countones` 903 → 741 µm², `br_counter_incr` 442 →
  384, `mul` 1738 → 1615, `br_amba_axi2axil` 14976 → 13654, `barrel_shifter`
  206 → 165.
- pass.abc wall on `br_amba_axi2axil`: ASAP7 1513 → 1597 ms, sky130 640 →
  900 ms (the second mapping runs only where the first met its budget).

A delay target that arrives ONLY through the graph-embedded `coloring_info`
`region_opts` channel still sizes and times against the SCL library, but a
run-level `delay` or a `--set` `region_opts` delay is what enables the SCL
path in `start()`.

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
(`Abc_NtkDelayTrace` over the unit-delay GENLIB — logic depth, the same
estimate ABC's `print_stats` shows after mapping; with a `delay` target and an
NLDM Liberty, see above, the delay/area are instead the SCL `stime` numbers in
picoseconds, the same timer the budget ladder judged by)
plus the **worst-arrival region output**, which stays the `Abc_NtkDelayTrace`
worst output, source-attributed to `file:line` through the output driver's
`srcid`. One line
per region goes to the step log, a `pass.abc qor:` summary follows the region
loop, and with `qor=FILE` the whole thing is written as JSON:

```json
{"schema_version":1, "top":…, "library":…, "seq":…, "delay_target":…,
 "total":{"regions":N,"input_nodes":IN,"input_ge":IGE,
          "gates":G,"area":A,"max_delay":D,
          "area_candidate_won":n,"delay_candidate_won":m,
          "critical_region":…, "critical_output":…, "critical_src":"file:line"},
 "regions":[{"module":…,"color":C,"input_nodes":in,"input_ge":ige,
             "gates":g,"area":a,"delay":d,"resynth":0|1,
             "budget":ps,"candidate":"area"|"delay",
             "delay_flow":{"delay":d1,"area":a1},"area_flow":{"delay":d2,"area":a2},
             "critical_output":…, "critical_src":…}, …]}
```

`budget`/`candidate`/`delay_flow`/`area_flow` are the mapping objective's
decision record (see "A budget is a budget in BOTH directions"): the region's
ps budget, which mapping it kept, and what each looked like to the SCL timer
when the choice was made. They are diagnostic only — absent on a cache hit and
on any region where no comparison ran — and `area_candidate_won` /
`delay_candidate_won` in `total` count the decisions, so "the candidate never
qualifies here" and "it qualifies and loses" are distinguishable without
reading every row.

`lhd pass abc --workdir W` defaults `qor` to `W/qor.json` and embeds the file
as the result envelope's `"qor"` member, so an agent loop reads its score
straight from `--result-json`. **Per-region numbers only**: the delay is ABC's
mapped estimate inside one region — paths crossing region or blackbox
boundaries are invisible here (`pass.opentimer` is the whole-design scorer).
A region whose flow fails contributes no row; a `delay` below 0 (or absent in
the JSON) means the mapped network exposed no delay data.

`--stats` keeps the aggregate/worst-path report and additionally renders every
`regions[]` object as one pretty line. A cold/full synthesis marks every row
`resynth=1`; an incremental run still reports every color, marking cache hits
`resynth=0` and only rebuilt misses `resynth=1`. JSON diagnostic output keeps
one object per color in the same `regions` array.

## Source-map carry-through

ABC's `strash`/`dch` destroy per-node provenance, so after mapping each gate is
re-attributed to the **original output cone** it feeds: each output port's driver
`srcid` is re-minted into the body locator (`import_from`), and one shared
backward traversal stamps each gate `Sub` from the lowest-index output cone that
reaches it (a stable primary anchor for gates shared by several outputs). The
output `Concat` glue carries the port srcid too. The
emitted netlist therefore points back to the pre-ABC RTL (verify with `cgen --set
cgen.srcmap=1`). Attribution is per-cone, not per-gate — ABC's optimization is
lossy, so exact gate lineage is unrecoverable.

## Incremental reuse (2opt-incr, `abc_incr.cpp`)

The oracle loop — edit a little RTL, resynthesize, compare QoR — changes a handful
of regions per iteration; the rest map to the *same* netlist. `--workdir W` turns
on a persistent per-region cache (under `W/abc_cache`) that skips ABC for any
region whose pre-ABC logic is unchanged (the one shared switch is
`--set lhd.incremental=false`; there is no per-pass cache flag). The cache is a **speedup, never an oracle
of record**: a miss only costs an ABC run, and every reuse is gated by an *exact*
structural compare (not a digest whose collision would miscompile). Regions
correspond by **module name**, and are stitched into the fresh wrapper by
**declared port name** (the partitioner emits content-stable, nid-free names), so a
hit needs no port matching.

```
Algorithm INCREMENTAL-ABC(design D, cell library L, persistent cache C)
  salt ← hash(L, seq/mem mode, recipe schema)      # global inputs the per-region compare can't see
  if C.salt ≠ salt: C ← ∅                           # a library/mode change invalidates the whole cache

  R ← PARTITION(D)                                  # one module per color region, children before parents
  for each region r ∈ R:                            # children-first, so a parent's child defs already exist
      r.pre  ← BUILD-PRE-BODY(r)                     # r's pre-ABC logic (same construction as the cold path)
      recipe ← RESOLVE-RECIPE(r, L)                  # the exact ABC script this region would run
      if r.name ∈ C  and  C[r.name].recipe = recipe  and  EQUIVALENT(C[r.name].pre, r.pre):
          netlist[r] ← C[r.name].mapped             # HIT — reuse the cached netlist in place; ABC never runs
      else:
          netlist[r] ← ABC-MAP(r, L, recipe)        # MISS — translate lgraph→ABC, optimize+map, read back
          C[r.name] ← ⟨recipe, r.pre, netlist[r], ports⟩     # store for the next run
  SAVE(C)
  return netlist

# "Is the fresh region logic identical to the cached one?"  Name-anchored and EXACT.
function EQUIVALENT(a, b)
  if STRUCT-IDENTICAL(a, b):   return true          # fast: one joint forward-signing pass
  if TRAVERSE-BIJECTION(a, b): return true          # exact fallback for genuine combinational loops
  return false

# Fast path. State cells and submodules are CUT POINTS, seeded across sides by hierarchical
# name; sign every node forward from its inputs to a fixpoint. Identical iff the node sets are
# in bijection AND every compare point (graph output, cut input) folds equal on both sides.
# Inconclusive when a real combinational loop stalls the signing.
function STRUCT-IDENTICAL(a, b) → {yes | no | inconclusive-cycle}

# Exact path. Build a real node bijection: pair the named sources (graph inputs, cut outputs,
# constants), walk both graphs in lockstep pairing each matched signal's consumers, then VERIFY
# every node is paired 1:1 and every edge maps consistently under the pairing. A traversal
# bijection cannot false-positive like a hash, so it decides the cyclic regions signing can't.
function TRAVERSE-BIJECTION(a, b) → {yes | no}
```

Boundary-change propagation is automatic and needs no invalidation logic: edit a
region and its `r.pre` differs → it misses and re-maps; a neighbor sharing the
changed boundary net also sees a different `r.pre` → it misses too; an
internals-only edit leaves every neighbor's boundary (hence `r.pre`) unchanged, so
only the edited region re-maps.

**Boundary naming (Proposal 2, canonical + reproducible).** Reuse stitches by port
name, so the names must be reproducible across recompiles. A boundary INPUT is
named by a **bidirectional** content signature — its *producer* cone (backward,
`cone_sig`) combined with its *consumer* cone (forward, `fwd_cone_sig`: how the
signal is used downstream, incl. the packed-state bit masks that pick a lane). Two
inputs collide only when they are structurally indistinguishable in BOTH
directions — a genuine boundary **automorphism** (truly replicated lanes). Such a
region is marked **reuse-ineligible** and always re-maps: `EQUIVALENT` cannot guard
it, because a true automorphism swaps inputs *and* outputs together, so the
name-anchored compare passes even when an arbitrary per-run tiebreak bound the
lanes' ports the opposite way, and the by-name stitch would wire the wrong nets
(observed as an LEC refutation before the input signature was made bidirectional).
The consumer cone dissolves the *false* ties — two lanes fed by identical logic but
used differently (e.g. writing different bits of one conflict-matrix flop) now get
distinct, reproducible names, so they become eligible and reuse soundly. Only the
genuinely-symmetric residue stays refused (a swap there is a real equivalence, so
refusing is merely conservative). Every eligible region goes through `EQUIVALENT`,
which reuses it when the reproducible naming matches and misses otherwise — never a
false reuse.

## Status

Combinational mapping (`seq=false`) is complete and LEC-verified
(`//lhd/tests:lhd_abc_test`), with source-map carry-through (above) and the
selectable `sum`/comparator bit-blast (`//lhd/tests:lhd_abc_arith_test`).
Sequential mapping (`seq=true`) — flops↔latches with name preservation +
single-root remap, and memory/`Sub` blackbox boundaries — is complete and
LEC-verified (`//lhd/tests:lhd_abc_seq_test`: flops, memory in both modes, and
a 3-level hierarchy) and is the default (`seq=true`). The memory bit-blast
(`memory=true`, above) is the default too: constant-address ports, per-lane
muxes, const/const forwarding and `read_all` are LEC-verified by
`//lhd/tests:lhd_abc_memlower_test` (a 32x8 constant-index multi-writer tile
through slang, proven with both lgyosys and cvc5, plus a gate-count guard) and
the write-priority case of `//lhd/tests:mem_ordering_test`. The `mult`/`shl`/`sra` bit-blasts
(array multiplier, constant + runtime barrel shifters) are LEC-verified by the
arith fixture. `div` (and `mod`, which lowers through `div`) stays blackboxed.
QoR read-back (gates/area/delay + `qor.json`, above) is in. Not yet
implemented: per-region `flow` overrides (2opt-freq C). See
`todo/livehd/2opt-freq.html`.
