# pass/domplace — provisional domino placement

Post-freeze code for [2d-domplace](../../todo/livehd/2d-domplace.html): it turns
USYN's frozen one-phase selection into a mixed CMOS/domino physical netlist
with provisional cell models. Everything here is opt-in. The logical
selection, its artifacts and the logical region cache are unchanged: the
logical target stays `cmos`, and this package sits outside `//pass/usyn:usyn_salt`
so that editing it never misses a logical cache entry. `//pass/domplace:domplace_salt`
keys its products instead: it hashes this package, `inou/lefdef` (LEF writer,
row access, pin access, routing rules, technology profiles), the Liberty
timing model and `usyn_salt`, which folds in `liberty_dff` (the reference
cell picks) through `synth_salt`.

The profile is a provisional model, not characterized PPA. Areas, widths and
timing coefficients are labeled estimates until a real cell builder replaces
them; the stage delays are a logical-effort estimate (below), not a
characterization.

## Stages

```
lhd synth top.v --set synth.mapper=usyn --set pass.usyn.clock_phases=1 --set pass.usyn.tmap=abc
                --set pass.usyn.physical=domino_provisional ...
 pass.usyn: per region  blast -> logical selection -> freeze      (unchanged, cached)
            physical selection over the frozen netlist          (post-freeze, never cached)
            emission: one body-less domino Sub per converted state, no Flop
            bundle:   <qor>.domino/{domino_cells.lef, domino_models.json, models/, domino_models.v}
            tmap:     residual CMOS + retained DFFs; domino Subs are preserved boundaries
 lhd:       domino LEF appended to the placement LEFs; bundle hashed into the physical cache
 place:     GP with 2H rectangles -> anchor two-row cells (rail template per start) -> terminal legalize -> release
```

One evaluation phase per converted stage (Q1). Each stage ends in one domino
gate plus its output storage; earlier logic stays CMOS. The schedule that
`stage_model.hpp` versions:

```
time             nT                        nT + T/2                  (n+1)T
source Q         launch --- CMOS + wire --- inputs stable --------------+
domino network   precharge                 evaluate                     |
output storage   holds previous Q ---------------------------------- capture
next stage Q                                                          + t_cq
ordinary stage   launch --------------- CMOS + wire ------------------ capture
```

- Into domino: `a + setup_eval + uncertainty_first <= T/2`.
- Inside domino: `eval_delay + setup_capture + uncertainty_second <= T/2`.
- Into an ordinary DFF: `a + setup_DFF + uncertainty_full <= T`.
- Precharge: `precharge + precharge_margin <= T/2`.

The functional model treats storage as an edge element, so cycle latency and
state correspondence are those of the source. There is no retiming and no extra
pipeline register.

## Eligibility and fallback

Physical eligibility is stricter than USYN's logical eligibility. Every state
the table does not admit keeps its original CMOS+DFF implementation and
reports its reason code (`reason.hpp`).

| Source state | v1 decision | Reason code |
|---|---|---|
| Positive edge, one-bit clock driven directly by a design-top input in the selected domain | candidate | — |
| Enable (already folded into D) | candidate | — |
| Positive edge on a recognized clock gate of the domain (folded, U21: below) | candidate: CLK on the root, the gate enable a data input | — |
| Synchronous or asynchronous reset to a known constant (U22: below) | candidate: the reset drives the storage RST pin | — |
| Reset value not a known constant bit | retained | `reset-value` |
| Synchronous reset computed inside the region (no input or register to wire RST to) | retained | `reset-internal` |
| The selection cut folded the reset into upstream CMOS / the function does not load the value while asserted | retained | `reset-absorbed` / `reset-shape` |
| Tied (constant) asynchronous reset | retained | `async-reset` |
| Power-on initial value | retained | `initial-value` |
| Negative edge | retained | `negative-edge` |
| Unknown edge | retained | `unknown-edge` |
| A folded gate's hold does not fit the PDN (no direct Q under one enable rail) | retained | `enable-stack` |
| A gate on a gated clock / without a phase-safe enable storage / sampled by another clock | retained | `gated-chain` / `gated-unsafe` / `gated-mixed` |
| An inverted clock / a register output or divider as the clock | retained | `inverted-clock` / `divided-clock` |
| The gate's enable cone holds state or hierarchy | retained | `gated-enable-state` |
| A gate sampling its enable at the falling edge without `domino_fall_gates` | retained | `gated-fall-sample` |
| A gated register with a synchronous reset (it resets only on enabled edges) | retained, the gate kept | `gated-sync-reset` |
| A recognized in-region ICG the fold left alone (another domain, say) | retained | `gated-clock` |
| Clock control is not a direct design input (logic or a crossing) | retained | `derived-clock` |
| Multibit clock / constant or absent clock | retained | `multibit-clock` / `constant-clock` |
| Clock port is not `domino_clock` | retained | `clock-domain` |
| Clock is a port of a preserved definition (each instance may clock it from a different root) | retained | `definition-clock` |
| Memory or opaque scope, transparent latch, ICG latch | retained (barrier) | `memory` / `opaque` / `transparent-latch` / `icg` |
| One-literal endpoint | retained unless `domino_identity=true` | `identity-endpoint` |
| No support after cofactoring | always retained | `constant-endpoint` |
| No positive-output formula within stack, branches and inputs | retained | `polarity-illegal` |
| Positive-output search stopped at its own limits (cubes, formula nodes, factoring) | retained | `polarity-search` |
| CMOS+DFF estimate not larger (`domino_select=area`) | retained | `area` |
| CMOS cone could not be priced | retained | `cmos-estimate` |
| Physical selection work exhausted | retained | `selection-budget` |

A native barrier is never a frozen state: USYN keeps a memory one native block
(it is not bit-blasted into registers), so it has no endpoint row. The report
lists every barrier row of a region (`regions[].barriers`, its source label and
code) and counts them by code in `totals.barriers`, outside `states`. A memory
is always `memory`.

Gate and reset retention codes appear only for registers the earlier rows
admit; the fold's own refusals carry a gated register's code even when another
row would also retain it.

A converted state carries `domino-area`, `domino-tie` or `domino-all`. Under
`select=area` the comparison is an area proxy: the domino side is
`x * A_NOT + A_register` plus one `A_NOT` for every complemented rail, and the
CMOS side is the register plus the Liberty estimate of the D-cone logic that
only this state uses and that its rail producers do not also need. A strictly
smaller domino converts as `domino-area`; one equal to the CMOS side converts
as `domino-tie` (U25, below); a larger one retains as `area`. Equal means
within `kAreaTieUm2` = 1e-6 um^2 (`physical_select.hpp`), which only absorbs
floating rounding of equal Liberty sums: an ASAP7 site is about 0.0146 um^2.
`domino_area_margin_pct` raises the CMOS side by its share first, so with a
margin the tie is with the raised threshold.

## Area ties convert (U25)

Ruled 2026-10-26: "At equal size, Dominos should win because there is going
to be way less wiring routing because it's a big cell doing a lot of logic
inside." Under `select=area` an exact tie converts, with its own code
`domino-tie` so reports tell it from a strict win. Before the ruling a tie
retained CMOS; on NVDLA/partition_m 619 of the 1496 `area` retentions were
exact ties, and on lfsr all 31 (0.55404 + 0.04374 = 0.59778).

## Clock gates (U21)

Ruled 2026-10-26: a register on a recognized clock gate converts with the gate's
enable delivered to the domino gate as a data input and the original, ungated
clock on CLK, so the stage is an enable register, next = en ? f : Q, through the
plain enable path. The gate stays for the CMOS consumers it still has and is
dropped only when none is left. Sharing one ICG across dominos, or generating
local clocks next to them, is future work.

`synth::fold_gated_clocks` (`pass/synth/gated_fold.hpp`) runs under the profile
on the flat source of the design top right before partitioning (the
`Pass_partition` `prepare_src` seam), so a gate inside preserved definitions is
seen with its consumers (NVDLA's `slcg` -> `NV_CLK_gate_power` -> `CKLNQD12`).
A positive-edge register on a recognized gate of `domino_clock` becomes

```
clock_pin := root & gclk        (== gclk: an AND gate is root & its latched enable)
clock_pin := root | gclk        (latch-or: == gclk, the gate is root | ~its latched enable)
enable    := enable & en        (redundant on the gated clock)
```

with both ANDs in the register's color, and the gate's enable cone (test enables
included) cloned once, across instance boundaries, into a color of its own so
it reaches every consumer region as one input. Recognized gates, structurally:
`latch-and` (`clk & L`, L transparent while clk is low, en = its D) and
`icg-cell` (a Liberty `clock_gating_integrated_cell` instantiated by name, en =
enable | test enable); with `pass.usyn.domino_fall_gates` also `negedge-and`
(`clk & F`, F a negative-edge flop on clk) and `latch-or` (`clk | ~L`, L
transparent while clk is high). Those two sample their enable at the falling
edge, so they equal the cycle-level enable only while it holds from the falling
to the rising edge (the stage's own half-cycle contract); a two-phase LEC can
refute them, and they retain as `gated-fall-sample` by default. Anything else is
reported, never folded (the codes above).

Physical selection treats a folded register as an enable register: its stage
converts only when the PDN keeps the hold (Q a direct input under one enable
rail), `enable-stack` otherwise; the selection cut keeps en * f + !en * Q within
the stack and input caps by moving excess f upstream, as for any register, so
enable-stack is the case where not even the hold fits. A converted stage's CLK
reads the root clock; a retained folded register keeps its original gated clock
and drops the redundant enable (D with en at 1), and select=area prices that
cone. Region emission drops a folded gate's instance or enable latch once nothing
reads it. The usyn report's `physical.clock_gates` lists the folded registers by
gate kind, the refusals by reason and the dropped gates; each folded state lists
its root and gated clock inputs.

A register clocked through a port of a preserved definition stays
`definition-clock`: the shared definition is synthesized once before its
instances' parents, so its registers can take the enable only through a new
input port wired by every instance (each its own enable), with every instance's
clock port on a recognized gate of the same root. That is a pre-partition
interface change of the shared definition, outside the flat-source seam.

## Storage reset (U22)

Ruled 2026-10-26: the reset network is not converted to domino. A converted
register's reset leaves the PDN for a dedicated `RST` pin on the stage's output
storage, with the source's kind (synchronous or asynchronous), polarity and
value; the formula realizes only the data function (and a folded gate's
enable). Selection strips a synchronous reset by fixing its net at the inactive
level once it has checked that the function loads the reset value while it is
asserted; an asynchronous reset is outside D already (a leftover term is a
don't-care the storage overrides). The writer rebuilds each rail producer with
the reset net held inactive, so no rail reads the reset even through logic the
selection cut shares with it. `RST` reads the async-reset control, or the region
input or register a synchronous reset pin reads.

Master identity, the declaration (`RST` after the rails), the lg model (a flop
with that reset, asynchronous when the source's is) and the Verilog model, the
LEF pin and the footprint carry the reset. A reset adds the area of the
Liberty's smallest asynchronous-reset register over the base register, else a
labeled one-inverter estimate (`reset_storage` in the sidecar). The reset net
stays CMOS: it gets the reset buffer tree of every mapper before placement
(`synth.place.reset_fanout`, default 32, pass/place; U23). The stage timer gives `RST` no
check and counts `reset->domino` under `coverage.false_paths`; it never enters
the period or the input->domino class.

## Rail buffering (U24)

Ruled 2026-10-26 ("yes if it improves timing"): under the profile, a net on
domino data rails with more than `synth.place.domino_rail_fanout` (default
32, 0 off) cell loads gets the pre-placement buffer tree of the reset nets
(pass/place `reset_tree.hpp`, buffers `__place_railtree*`). The per-region
mapping never buffers a rail, and a 1344-pin rail limited sha3. The sweep on
HighTide (ORFS frame, arm 3): sha3 3967 -> 2226 ps with 0/0 max-cap/max-transition
violations (16: 2210 ps for 2.6x the buffers; 64: 2661 ps, 69/62 violations);
lfsr unchanged (no rail above 32 loads).

## Model schema

`stage_model.hpp` holds `Stage_model` version 1: the clock domain, the
stage-wide schedule coefficients in ps (`std::nullopt` is a reported missing
value, never an implicit zero) and `storage_init = 'x'`, since v1 converts no
initialized state. The evaluate and precharge delays depend on the gate, so
they are per master variant, not in `Stage_model`.

Master identity is exact Boolean identity: equal input, transistor and stack
counts never make two functions interchangeable. A master is named
`__lhd_domino_<h16>_v<k>_t<t>` (reserved prefix, identity hash, drive variant,
geometry template) with pins `R0..R{n-1}` (physical rail order: per logical
input, positive rail then negative rail), `CLK` and `Q`.

## PG rail templates

A two-row cell spans three row edges: outer, inner, outer. Rows alternate
N/FS, so the rail on a row edge alternates between the reference inverter's
bottom and top PG rail, and one rail stack fits only every other start. An
OpenROAD `check_placement` of the HighTide lfsr run rejected exactly the
dominoes on FS starts (DPL-0004). Every variant is therefore written in two
geometry templates with the same function, signal pins, drive and footprint:

| Template | Rails at 0, H, 2H | Fits starts on | ASAP7 | sky130 |
|---|---|---|---|---|
| `t0` | inverter bottom, top, bottom | N (and FN) rows | VSS VDD VSS | VGND VPWR VGND |
| `t1` | inverter top, bottom, top | FS (and S) rows | VDD VSS VDD | VPWR VGND VPWR |

The rails derive from the reference inverter's PG pins and the row
orientation (`template_rails`); nothing is technology-specific. Synthesis
instantiates `v0_t0`. Placement takes the template each start row needs: the
anchor prepass and the two-row ECO swap to the rail twin (`pass/place`
`Multirow_template::rail_twin`), moving the netlist instance with the
floorplan, and a restored placement re-derives it from its rows. Templates
rather than restricted starts keep every row pair usable, which tiny cores
need; a master without a twin (a sidecar may omit `rail_twin`) is
restricted to its own parity. `check_legality` checks the parity of every
multi-row cell (`pass/place/pg_rails.hpp`), so a wrong template never passes.

The bundle sidecar (`domino_models.json`, schema 6) records the stage model
with each coefficient's source (`stage_model.sources`), units (`ps`, `fF`,
`um`, DBU), the corner, technology and row-access identity, the named
reference inverter and register family (with each register's pin names and CK
capacitance), FO4 and the delay model (`delay_model`: source, citation,
formula, parameters, tau and the FO4 derivation), the coefficients and
experiment knobs, the reset storage term (`reset_storage`), every master
(formula, rails, storage `reset` with its pin, kind, polarity and value, metrics, variants with
estimated and rounded areas, pin caps (one per rail, U26), per-variant capture setup, per-variant
`timing` with the evaluate and precharge delays and their logical-effort
terms, access checks; each variant in both rail templates with its
`template`, `pg_rails`, `start_orientations` and `rail_twin`), the row rails
it derived them from (`technology.row_rails`) and every instance with the
original state it owns. A coefficient that cannot be derived is `null` with a
diagnostic; `read_domino_sidecar` reads schema 6 only, refuses another schema
by number (regenerate the bundle), and returns the variant catalog (one
family per master and template), the row-access templates and the rail
twins for placement. Schema 5 added the storage reset pin (U22); schema 6 the
per-rail pin capacitances (U26).

Every stage coefficient has a built-in, labeled value under the profile (the
U1 ruling; see Stage delay estimate). `pass.usyn.domino_eval_fo4`, `domino_precharge_fo4`,
`domino_setup_eval_fo4` and `domino_precharge_margin_fo4` (FO4 multiples) and
`domino_uncertainty_first_ps` / `domino_uncertainty_second_ps` override it
coefficient by coefficient (source `option`); an unset uncertainty is the
labeled ideal clock, 0 ps, as `synth.place.stage_timing.clock_uncertainty_ps`
defaults to 0. The CLK load is a labeled estimate: the register's CK input
plus `domino_clock_devices` (default 2) clocked devices at half a
reference-inverter input each. Two experiment knobs keep every default
unchanged: `domino_pair_area_pct` (default 100) scales only the `x * A_NOT`
term on both area bases, and `domino_area_margin_pct` (default 0) converts
under `select=area` while `A_domino + A_rails <= (100 + margin)%` of the CMOS
estimate (equality is the U25 tie).

## Stage delay estimate

`domino_delay.{hpp,cpp}` is the one place the formula lives. It applies the
Sutherland-Sproull-Harris logical-effort model of a footed domino gate
(I. Sutherland, R. Sproull, D. Harris, *Logical Effort: Designing Fast CMOS
Circuits*, 1999, the circuit-families treatment of domino gates; also
N. Weste, D. Harris, *CMOS VLSI Design*, logical effort of dynamic gates) in
units of the technology's tau. Each converted stage is a
footed dynamic pull-down, its output inverter and the variant's register.

FO4 is measured on the reference inverter's Liberty NLDM tables: the inverter
drives four of its own inputs (`4 C_in`, `C_in` the larger of its
`capacitance`, `rise_capacitance` and `fall_capacitance`), each edge's input
slew is the opposite output transition at that load (iterated to a fixed
point, as in an FO4 chain), and FO4 is the mean of the rise and fall delays.
Slew axes and transitions share the Liberty's thresholds, so nothing is
derated. `tau = FO4 / (4 + p_inv)`.

With `s` the formula's series stack, `b` its parallel branches, `n` its PDN
transistors (`Gate_formula` metrics), `C_D` the variant register's D input
and `C_in` the reference inverter's input. Widths are in units where the unit
inverter is NMOS 1 + PMOS 2 (input 3, pull-down resistance 1, and
`tau = R * C / 3` per unit). Each pull-down device is an NMOS of width
`x = 1.5`, half a reference-inverter input (U26, below):

- the footed stack (`s` devices and the foot) has resistance `(s+1)/x`; its
  logical effort is `(x/3) * (s+1)/x = (s+1)/3` and its electrical effort the
  output inverter (`3k`) over one device input (`x`), `h = 3k/x`;
- the dynamic node carries `b` branch-top drains of width `x`, the precharge
  drain `w`, and `n - b` devices below a branch top at weight `l`, so its
  parasitic delay is `(s+1)/x * p_inv (x b + x l (n-b) + w) / 3`;
- precharge charges the output inverter gate (`3k`) and the branch-top and
  precharge drains (`p_inv (x b + w)`) through a PMOS of drive `w/2`.

```
eval      = tau * [ (s+1)/3 * 3k/x                       footed pull-down: g = (s+1)/3, h = 3k/x
                  + p_inv (s+1) (b + l (n-b) + w/x) / 3   dynamic node: b branch tops, the precharge drain,
                                                          n-b internal devices at weight l
                  + C_D / (k C_in) + p_inv ]              output inverter into the register D input
precharge = tau * 2 (3k + p_inv (x b + w)) / (3w)         PMOS of drive w/2 charging the output inverter
                                                          gate and the dynamic-node diffusion
setup_eval = 0.5 FO4      precharge_margin = 0.5 FO4
```

At `x = 3` (one reference-inverter input per device, the model before U26)
this is `h = k`, `w/3` and `3b`. With `x = 1.5` a device draws half the input
current and has twice the resistance, so the pull-down effort doubles
(`h = 2k`) while its drains halve.

| Parameter | Default | Meaning |
|---|---|---|
| `p_inv` | 1 | reference-inverter parasitic delay in tau (FO4 = 5 tau) |
| `pulldown_width` `x` | 1.5 | one pull-down NMOS, half a reference-inverter input (U26); the rail pin capacitance counts the same device |
| `precharge_width` `w` | 2 | precharge PMOS width, a unit inverter's pull-up |
| `internal_load` `k` | 1 | the output inverter on the dynamic node, in reference-inverter inputs (the internal-load assumption; the keeper is not modeled) |
| `internal_node_weight` `l` | 0.5 | diffusion of a device below a branch top, relative to one on the dynamic node |
| `setup_eval_fo4`, `precharge_margin_fo4` | 0.5, 0.5 | FO4 fractions |

A deeper stack is slower (logical effort and parasitic both grow with
`s + 1`, the foot included); more branches or more devices are slower (more
diffusion on the dynamic node); precharge grows with the branches. Internal
stack nodes are not precharged and charge sharing, keeper contention and
noise margins are not modeled. A missing NLDM arc or input capacitance leaves
FO4 and everything it scales `null`, a register without a D capacitance
leaves its variants' delays `null`; either is a bundle diagnostic and a
`domino-delay` warning from pass.usyn, and the stage timing then reports the
class unmeasured. On the sky130 fixture (inv_1, dfxtp_1) FO4 is 82.91 ps and
an AND2/AND3/AND4 master evaluates in 108.4/146.2/189.5 ps with a 35.9 ps
precharge (`domino_delay_test`; before U26, with width-3 devices,
80.8/109.3/143.4 ps and 44.2 ps). On ASAP7 (INVx1, DFFHQNx1, FO4 14.37 ps)
they are 19.4/25.9/33.4 ps with a 6.2 ps precharge (before:
14.6/19.5/25.4 ps and 7.7 ps).

## Rail pin capacitance (U26)

Ruled 2026-10-26: "the input capacitance, if it's only using one of the
stacks, the variable should be half of a NOT gate. But if the variable is
using three stacks, it should be 1.5 times the NOT gate." A rail pin loads its
driver with the PDN devices it gates: `m * C_in / 2`, `m` the literal
occurrences of its (input, polarity) in the checked formula, repeated
occurrences included (the counts sum to `n`; a complemented rail is its own
pin with its own count). It is the same half-inverter device the delay
estimate sizes (`pulldown_width`), so the input load and the pull-down drive
agree. The sidecar records each rail's `pdn_devices` and each variant's
`pin_caps.rails_ff` (pin order) with its source label; the stage timer gives
each rail pin its own capacitance. The storage reset pin keeps the earlier
labeled one-reference-inverter-input estimate (`pin_caps.reset_ff`: one reset
transistor, not characterized), and CLK and the Q arcs are unchanged.

## Stage timing

`timing/` (outside the `domplace_salt` glob) times the placed mixed netlist
with the native late proxy (`pass/place/late_timing.hpp`). One provisional
cell per bundle variant (`domino_timing_cells`): rails with a `setup_eval`
setup arc to CLK, CLK with the labeled capacitance, and Q with the variant
register's own clock-to-Q arcs; rails capture at `T/2 - uncertainty_first -
setup_eval`, so a rail's period estimate is `T - slack / 0.5`. Each instance
adds two constant checks that moving cells cannot fix: internal
`2 (eval + setup_capture + uncertainty_second)` and precharge
`2 (precharge + precharge_margin)`, with the evaluate and precharge delays of
the instance's own variant (a stage-wide override wins). A null coefficient is
reported, never read as zero. A storage reset pin (`RST`, U22) has no check:
reset to RST is a false path, counted under `coverage.false_paths`
(`reset->domino`). The native proxy carries the ideal clock through inverters,
ICG cells and recognized clock gates (`pass/place/README.md`), so registers on
gated clocks are timed in every arm, and each gate enable's clock-gating setup
check is the `gate` capture class.

Constraints come from the saved SDC when it parses, else one ideal clock on
the named port (a non-50% waveform is refused: the schedule is a fixed T/2).
Unconstrained ports, and endpoints with no constrained launch in their
fan-in, are cut and listed with their reasons. The status is `complete`,
`partial` (something cut) or `incomplete` (uncertified, or a needed
coefficient null). `stage_timing.json` (schema 1) reports the model
provenance, clocks, constraints, the period by class (flop, domino and input
launches into flop, domino and output captures, from launch-filtered
evaluations), coverage with `early: unknown` (setup only), every stage with
its source state, path split and CLK load, the endpoints, clock accounting
(clock-pin capacitance per sink instance, summed by master, which names the
domino variant, and by domain, plus the placement RC wire estimate; no CTS
power or skew), electrical limits and area on both bases.

`lhd synth --set synth.place.stage_timing=true` runs it after placement,
fresh or restored, with or without the profile, so every arm has the same
timer: the clock is `synth.place.stage_timing.clock_port`, else
`pass.usyn.domino_clock`, else the SDC's one clock port, at the period and
waveform of the SDC clock on that port (an SDC with several clocks and none
on the port times only through the saved-SDC subset, never at another
clock's period); `synth.place.stage_timing.require_complete` fails short of
complete coverage and `.clock_uncertainty_ps` sets the full-cycle
uncertainty. It writes `place/stage_timing.json` and `qor.place.stage_timing`
and is never part of the placement cache identity. A netlist that keeps
word-valued ports is timed on its bit-level DEF import, which also reads the
placement's `memory_proxies.lef`: a native memory (USYN keeps every memory one
block) is its virtual proxy macro there, without a Liberty model, so the
report is `incomplete` (uncertified, `uncharacterized cell`) rather than a
failure. `//tools:stage_timing_probe`
reruns the same report offline on a saved placement, with coefficient
overrides for sweeps that must not re-place.

## Sizing

`lhd synth --set synth.place.domino_sizing=true` (with the profile and
`synth.place.stage_timing=true`) sizes the placed domino cells after final
legalization, without rerunning USYN (`pass/place/custom_sizing.hpp`). The
choices are the sidecar's variant catalog: the variants of one master and
template, ranked by their register's drive, with the same rails, CLK, Q and
port order. A domino instance whose Q slack is negative steps up (slack
in period terms: the stage timer divides each requirement's slack by its
capture fraction, so a half-cycle ps counts twice); once its
top variant is in place, or its up move was rejected, its Q net is buffered
(the smallest inventory buffer takes every sink but the worst;
`.buffers=false` never buffers); an instance with spare slack steps down.
Every trial is one edit re-placed through the two-row ECO (in place, a shift
of up to 64 sites, displacing up to 16 single-row cells, else a re-anchor,
with the terminal padding kept) and re-timed with the same stage timer; it
is accepted when the stage period is shorter and electrical violations are
not worse, or when the period is not worse and setup WNS/TNS (period terms)
and electrical violations are not worse with some gain (LEF footprint area
counts). A packing failure is a rejected trial, and so is any other trial
failure, such as a variant whose register leaves the stage period
unmeasured (`rejected_failed`, with its reason). Every trial also shares full
placement's 3% cell/area growth budget (`pass/place/README.md`) with the other
placement edits, against the mapped netlist (domino cells at variant 0): a step
or buffer past it is `rejected_growth` before any trial placement, and an
up move rejected that way is followed by the buffer. `.trials` (default 64) bounds
the trials. The netlist is always variant 0 at synthesis and an instance
that stepped up never steps back down, so through lhd no instance steps down
yet (the engine's down path is tested on its own). `place/domino_sizing.json` and
`qor.place.domino_sizing` list each trial (with its two-row ECO block), the
variant counts, the C_clk change from the catalog's labeled `clock_ff`, and
`eco`, the two-row ECO summed over the accepted trials
(`qor.place.multirow_eco` describes only the last ECO place() call). With sizing on, the stage
timing settings and the stage-timing code salt join the physical cache key.

## Code

| Target | Contents |
|---|---|
| `contract` | `stage_model.hpp` (versioned schedule), `reason.hpp` (stable reason codes) and the salt |
| `domino_reference` | the named reference inverter (with its NLDM cell) and register ladder (with D and CK capacitance), and `domino_area` |
| `domino_delay` | the logical-effort stage-delay estimate: FO4 from the inverter's NLDM, evaluate and precharge per master variant |
| `physical_eligibility` | the eligibility table above over a frozen `Stateful_region` |
| `domino_cell` | exact master identity, `__lhd_domino_*` declarations (Q is a loop break) and behavioral lg/Verilog models |
| `physical_select` | the per-state choice: recheck, cofactor/merge inputs, positive-output resynthesis, rail ledger, area comparison, bounded and deterministic |
| `physical_report` | the `physical` block of pass.usyn's report |
| `domino_geometry` | two-row `CLASS CORE` abstracts on the reference inverter's site (`SIZE w BY 2H`, `SYMMETRY X Y`, PG at 0/H/2H in rail template t0 or t1), the exact site quantization, row-aware pin access checks on the starts each template's rails fit, and the route policy (Tech_profile, else the LEF routing stack) |
| `domino_bundle` | the `<qor>.domino/` bundle and the placement-side sidecar reader |

The bundle holds `domino_cells.lef` (macros only: no UNITS or SITE, read after
the technology and cell LEFs), `domino_models.json` (the sidecar),
`domino_models.v` and `models/` (an lg library for `lhd lec --lib lg:`), and
`row_access.json` (the convention it was generated for). Every master is
written in every register-ladder variant, each in both rail templates; the
netlist instantiates `v0_t0`, placement picks the template and only
`synth.place.domino_sizing` swaps variants after placement. An abstract that
fails the pin-access check, or a rung whose two templates do not together fit
both start parities, refuses the bundle and pass.usyn publishes nothing; a technology rule the access
engine cannot check (sky130's mcon enclosure) is reported as unverified, never
as passed.

`pass.usyn.physical=domino_provisional` runs `physical_select` on every region
after freeze, a logical cache hit included, through
`usyn::Design_options::physical` (`usyn/physical_emission.hpp` is the data
seam), and emits the mixed netlist with `usyn::write_physical_module`: each
converted state is one body-less `__lhd_domino_*` Sub named after the state,
whose rails are driven by the frozen producers (a complemented producer
through an explicit inverter) and whose CLK is the region's clock input; its
Flop and the D logic only it used are gone, and retained states keep their
Flops. The technology map (`pass.usyn.tmap=abc`) maps the residual CMOS and
the retained registers and preserves the domino instances as boundaries
(`pass/synth/tmap.hpp`). The `tmap_trials=2` comparison charges each domino
instance its Liberty-basis estimate.

`<qor>.usyn.json` reports `output` as `logical-mixed`/`mapped-mixed`,
`physical_profile`, and a `physical` block: settings, reference cells, totals
by reason with both area bases (`a_domino_estimate` on the Liberty basis,
`a_footprint_um2` from the bundle's rounded LEF footprints), native barriers
by code (`totals.barriers`, per region `barriers`), one row per state
with its master, `n_pdn`, `x`, area terms and rails, and the published bundle.
Areas are proxies, not characterized PPA.

Each converted instance is named after its state, also through the U21 enable
fold and the U22 RST pin, and placement keeps that name under transparent
region scopes (pass/place), so `lhd lec` pairs every domino storage with its
source register by name. `lhd_usyn_memory_lec_smoke.py` checks that with an
enable, synchronous resets and a Liberty clock gate (no unpaired state,
PROVEN-full), and that a memory stays a `memory` barrier. A placement that
holds both two-row domino cells and a memory proxy macro is not supported yet
(the macro stage refuses movable two-row cells).

## lhd

`lhd synth` (and `lhd pass usyn`) plumb the profile's INTERNAL labels:
`pass.usyn.lef` from `synth.lef` (the placement LEFs) and `pass.usyn.row_access`
from the positional `livehd.physical.row_access` metadata record (a schema-1 `convention` object; absent uses the stand-in); setting either directly is a usage error. An unset
`pass.usyn.domino_clock` is taken from an SDC whose only clock is
`create_clock ... [get_ports P]`; otherwise it must be named.

The profile never moves a default; what it cannot support is a usage error:
another mapper, STA (`synth.opentimer=false` is required: the cells have no
Liberty timing), timing-driven placement, resize and resynthesis, and the
detail-final region consumers (busorder, boundarypins). `synth.place.pg` is
allowed: the native grid derives row rails from one-row cells only, and the
rail templates match them (checked on the lfsr ORFS frame with handoff taps:
OpenROAD `check_placement` passes). It needs what any PG run needs (handoff
taps, a core inset, DEF tracks). OpenTimer and the cgen simulator refuse a `__lhd_domino_*`
instance by name (`core/provisional_cell.hpp`).

After the mapper, lhd reads the bundle (`read_domino_sidecar`), appends
`domino_cells.lef` to the placement LEFs (so the DEF export's `lefs.json` and
every LEF reader see it), adds the LEF and sidecar to the physical cache
dependencies, and exports the bundle with `--emit-dir report:`.
`//lhd:physical_salt` hashes `usyn_salt` and `domplace_salt`.
`lhd/tests/lhd_domplace_smoke.py` checks the refusals, the report and sidecar,
and sequential LEC: proven with `--lib lg:<qor>.domino/models` beside the
gensim cell models, not proven without them.
`lhd_domplace_cases_smoke.py` places the H cases: retained `clock-domain` and
`identity-endpoint` states, a complemented converted-Q rail (a real
inverter), a converted Q shared by its output port and another domino's rail,
and a 12-literal endpoint whose excess moves into upstream CMOS. It checks
placed-netlist LEC and three replays in one workdir: warm (logical and mapped
hits, byte-identical bundle, restored placement), `domino_aux=5` (logical and
mapped hits, new bundle, fresh placement), and an all-CMOS control under the
profile. `lhd_domplace_floorplan_smoke.py` places into an imported pinless
frame narrow enough to fit only one anchor per row pair. One site narrower,
it checks the named atomic packing failure. The smokes check that each
anchor carries the rail template of its start row
(`domplace_view.rail_template`). Tiny area-driven cores: the legalization
bins are at least two rows tall when a two-row cell is movable (the lfsr
core at utilization 0.55 failed the anchor prepass on one-row bins; see
`pass/place/README.md`). Imported DEF placement blockages
cannot fragment rows here yet, because terminal legalization does not consult
them. `lhd_domplace_sizing_smoke.py` writes a second register rung (a
stronger `dfxtp_2`, 4 sites wider, so a step fits the 3% growth budget) to its
temporary directory and sizes: an up-size that displaces a neighbour, LEC with
the bundle models, a warm replay, a growth a dense core cannot pack, whose DEF
equals the unsized run's, a 24-site rung whose step is `rejected_growth` (DEF
again unsized), and an up-size then an output buffer that isolates the one
critical flop load from 32 unconstrained ones.
`lhd_domplace_gates_smoke.py` (U21) appends the sequential fixture fragment
(`inou/lefdef/tests/data/sky130hd_seq.*.fragment`: ICGs, latches, a reset
flop) and covers an inline latch + AND gate with a test enable, a Liberty ICG
with its test enable, a gate inside a definition wrapped twice (all three
LEC-proven, gates dropped), enable-stack on a one-device stack, the
definition-port case and the inverted ones. `lhd_domplace_reset_smoke.py`
(U22) converts synchronous, enabled-synchronous, asynchronous and active-low
resets onto RST, checks that no rail reads a reset, the false-path count,
the buffered reset net and placed-netlist LEC.

## Matched commands

`scripts/domplace/arms.py` runs the three arms with matched settings,
serialized: arm 1 (ABC CMOS), arm 2 (USYN CMOS) and arm 3 (USYN with this
profile). Every arm pins the seed, `synth.place.iterations`, `density` and
`grid.density`, turns off timing-driven placement, resizing, resynthesis,
`io_resynth` and OpenTimer, and times its final placement with the same
native stage timer (`synth.place.stage_timing`). Arms 2 and 3 pin
`pass.usyn.tmap_trials=1`, so the domino arm keeps arm 2's USYN trial instead
of a different one (domino instances count as gates in `mapped_better`).

```
scripts/domplace/arms.py --rtl top.v --top T --liberty L --lef T,C --sdc S \
    --core-mode both --warm [--sta-leg] [--clock-port P] \
    --arm3-set pass.usyn.domino_eval_fo4=2 --arm3-set pass.usyn.domino_precharge_fo4=2 \
    --arm3-set pass.usyn.domino_setup_eval_fo4=0.5 --arm3-set pass.usyn.domino_precharge_margin_fo4=0.5 \
    --arm3-set pass.usyn.domino_uncertainty_first_ps=0 --arm3-set pass.usyn.domino_uncertainty_second_ps=0
```

The coefficients above are only an example of overrides: without them arm 3
uses the built-in estimate, and its stage timing is `partial` only for the IO
cuts.
`--core-mode` reports the area-driven core (`--utilization`) and the fixed
core (the core and die of the arm-1 area-driven run, as
`synth.place.core`/`die`) separately. Each leg records wall time, the peak
RSS of its whole process tree (sampled from `/proc`), LEC against the RTL
(gensim cell models, plus the bundle models for arm 3), a status class
(`ok`, `unproven`, `unsupported`, `infeasible` for a packing failure,
`timeout`, `failed`), coverage, area on both bases (rail inverters are
netlist cells and are counted once), `qor.place.multirow`/`multirow_eco`
(with sizing, the ECO displacement is `domino_sizing.eco`'s total),
the stage period, classes and C_clk, HPWL and sizing counts. `--warm` reruns
each leg twice in its workdir and records the second. Output goes to the
ignored `repros/domplace_<date>/`: `manifest.json` (HEAD, dirty-diff hash,
lhd hash, input hashes, options, commands), `results.json`, `results.tsv` and
each leg's workdir, DEF/LEF and bundle. `--dry-run` writes only the manifest;
`scripts/domplace/arms_smoke.py` checks the plan and runs arms 1 and 3 on the
in-repo fixture. Final RUDY is not reported (plan H3 is not implemented).

Designs must map every retained register with the Liberty's register family
(the 7-cell sky130 fixture has no reset or negative-edge flop). HighTide
runs (plan H5) are approved but not yet integrated: patching `lhdsweep.py` to
score arm 3's OpenSTA timing as unmeasured and use the native stage period
for every arm (U14) is pending. The fixed core comes from the arm-1
area-driven run (U15); the placer's FS-import fix that DEF floorplan import
needed has landed (`make_arrays` records CORE row flips in the native node
orientations).
