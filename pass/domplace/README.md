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
them.

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
 place:     GP with 2H rectangles -> anchor two-row cells -> terminal legalize -> release
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
| Enable or synchronous reset (already folded into D) | candidate | — |
| Asynchronous reset | retained | `async-reset` |
| Power-on initial value | retained | `initial-value` |
| Negative edge | retained | `negative-edge` |
| Unknown edge | retained | `unknown-edge` |
| ICG-gated clock (the clock root is a recognized clock gate) | retained | `gated-clock` |
| Clock control is not a direct design input (logic, an inverter, a divider or a crossing) | retained | `derived-clock` |
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

A converted state carries `domino-area` or `domino-all`. Under `select=area`
the comparison is an area proxy: the domino side is `x * A_NOT + A_register`
plus one `A_NOT` for every complemented rail, and the CMOS side is the
register plus the Liberty estimate of the D-cone logic that only this state
uses and that its rail producers do not also need. A tie retains CMOS.

## Model schema

`stage_model.hpp` holds `Stage_model` version 1: the clock domain, the
schedule coefficients in ps (`std::nullopt` is a reported missing value, never
an implicit zero) and `storage_init = 'x'`, since v1 converts no initialized
state.

Master identity is exact Boolean identity: equal input, transistor and stack
counts never make two functions interchangeable. A master is named
`__lhd_domino_<h16>_v<k>_t<t>` (reserved prefix, identity hash, drive variant,
geometry template) with pins `R0..R{n-1}` (physical rail order: per logical
input, positive rail then negative rail), `CLK` and `Q`.

The bundle sidecar (`domino_models.json`, schema 2) records the stage model,
units (`ps`, `fF`, `um`, DBU), the corner, technology and row-access identity,
the named reference inverter and register family (with each register's pin
names and CK capacitance), FO4 as a labeled estimate, the coefficients and
experiment knobs, every master (formula, rails, metrics, variants with
estimated and rounded areas, pin caps, per-variant capture setup, access
checks) and every instance with the original state it owns. Unset
coefficients are `null` with a diagnostic; `read_domino_sidecar` reads
schema 2 only and returns the variant catalog for placement-side sizing.

The stage coefficients have no built-in values: `pass.usyn.domino_eval_fo4`,
`domino_precharge_fo4`, `domino_setup_eval_fo4` and
`domino_precharge_margin_fo4` (FO4 multiples) and
`domino_uncertainty_first_ps` / `domino_uncertainty_second_ps` set them, and
an unset one leaves its timing class unmeasured. The CLK load is a labeled
estimate: the register's CK input plus `domino_clock_devices` (default 2)
clocked devices at half a reference-inverter input each. Two experiment knobs
keep every default unchanged: `domino_pair_area_pct` (default 100) scales only
the `x * A_NOT` term on both area bases, and `domino_area_margin_pct`
(default 0) converts under `select=area` while `A_domino + A_rails <
(100 + margin)%` of the CMOS estimate.

## Stage timing

`timing/` (outside the `domplace_salt` glob) times the placed mixed netlist
with the native late proxy (`pass/place/late_timing.hpp`). One provisional
cell per bundle variant (`domino_timing_cells`): rails with a `setup_eval`
setup arc to CLK, CLK with the labeled capacitance, and Q with the variant
register's own clock-to-Q arcs; rails capture at `T/2 - uncertainty_first -
setup_eval`, so a rail's period estimate is `T - slack / 0.5`. Each instance
adds two constant checks that moving cells cannot fix: internal
`2 (eval + setup_capture + uncertainty_second)` and precharge
`2 (precharge + precharge_margin)`. A null coefficient is reported, never
read as zero.

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
`pass.usyn.domino_clock`, else the SDC's one clock port, at the SDC clock
period; `synth.place.stage_timing.require_complete` fails short of
complete coverage and `.clock_uncertainty_ps` sets the full-cycle
uncertainty. It writes `place/stage_timing.json` and `qor.place.stage_timing`
and is never part of the placement cache identity. A netlist that keeps
word-valued ports is timed on its bit-level DEF import. `//tools:stage_timing_probe`
reruns the same report offline on a saved placement, with coefficient
overrides for sweeps that must not re-place.

## Sizing

`lhd synth --set synth.place.domino_sizing=true` (with the profile and
`synth.place.stage_timing=true`) sizes the placed domino cells after final
legalization, without rerunning USYN (`pass/place/custom_sizing.hpp`). The
choices are the sidecar's variant catalog: the variants of one master and
template, ranked by their register's drive, with the same rails, CLK, Q and
port order. A domino instance whose Q slack is negative steps up; once its
top variant is in place, or its up move was rejected, its Q net is buffered
(the smallest inventory buffer takes every sink but the worst;
`.buffers=false` never buffers); an instance with spare slack steps down.
Every trial is one edit re-placed through the two-row ECO (in place, a shift
of up to 64 sites, displacing up to 16 single-row cells, else a re-anchor,
with the terminal padding kept) and re-timed with the same stage timer; it
is accepted when the stage period is not worse and setup WNS/TNS and
electrical violations are not worse with some gain (LEF footprint area
counts). A packing failure is a rejected trial. `.trials` (default 64) bounds
the trials. The netlist is always variant 0 at synthesis, so a down-size
only follows an up-size. `place/domino_sizing.json` and
`qor.place.domino_sizing` list each trial, the variant counts and the C_clk
change from the catalog's labeled `clock_ff`. With sizing on, the stage
timing settings and the stage-timing code salt join the physical cache key.

## Code

| Target | Contents |
|---|---|
| `contract` | `stage_model.hpp` (versioned schedule), `reason.hpp` (stable reason codes) and the salt |
| `domino_reference` | the named reference inverter and register ladder, and `domino_area` |
| `physical_eligibility` | the eligibility table above over a frozen `Stateful_region` |
| `domino_cell` | exact master identity, `__lhd_domino_*` declarations (Q is a loop break) and behavioral lg/Verilog models |
| `physical_select` | the per-state choice: recheck, cofactor/merge inputs, positive-output resynthesis, rail ledger, area comparison, bounded and deterministic |
| `physical_report` | the `physical` block of pass.usyn's report |
| `domino_geometry` | two-row `CLASS CORE` abstracts on the reference inverter's site (`SIZE w BY 2H`, `SYMMETRY X Y`, PG at 0/H/2H), the exact site quantization, row-aware pin access checks, and the route policy (Tech_profile, else the LEF routing stack) |
| `domino_bundle` | the `<qor>.domino/` bundle and the placement-side sidecar reader |

The bundle holds `domino_cells.lef` (macros only: no UNITS or SITE, read after
the technology and cell LEFs), `domino_models.json` (the sidecar),
`domino_models.v` and `models/` (an lg library for `lhd lec --lib lg:`), and
`row_access.json` (the convention it was generated for). Every master is
written in every register-ladder variant; the netlist instantiates variant 0,
and only `synth.place.domino_sizing` swaps variants after placement. An abstract that fails the pin-access check refuses
the bundle and pass.usyn publishes nothing; a technology rule the access
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
`a_footprint_um2` from the bundle's rounded LEF footprints), one row per state
with its master, `n_pdn`, `x`, area terms and rails, and the published bundle.
Areas are proxies, not characterized PPA.

## lhd

`lhd synth` (and `lhd pass usyn`) plumb the profile's INTERNAL labels:
`pass.usyn.lef` from `synth.lef` (the placement LEFs) and `pass.usyn.row_access`
from `synth.place.row_access` (`default`, the stand-in, or a schema-1 JSON
path); setting either directly is a usage error. An unset
`pass.usyn.domino_clock` is taken from an SDC whose only clock is
`create_clock ... [get_ports P]`; otherwise it must be named.

The profile never moves a default; what it cannot support is a usage error:
another mapper, STA (`synth.opentimer=false` is required: the cells have no
Liberty timing), timing-driven placement, resize and resynthesis,
`synth.place.pg`, and the detail-final region consumers (busorder,
boundarypins). OpenTimer and the cgen simulator refuse a `__lhd_domino_*`
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
it checks the named atomic packing failure. Imported DEF placement blockages
cannot fragment rows here yet, because terminal legalization does not consult
them. `lhd_domplace_sizing_smoke.py` writes a second register rung (a
stronger, wider `dfxtp_2`) to its temporary directory and sizes: an up-size
that displaces a neighbour, LEC with the bundle models, a warm replay, and a
growth a dense core cannot pack, whose DEF equals the unsized run's.

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

The coefficients above are only an example: none is built in, and arm 3
without them reports its unmeasured classes and status `incomplete`.
`--core-mode` reports the area-driven core (`--utilization`) and the fixed
core (the core and die of the arm-1 area-driven run, as
`synth.place.core`/`die`) separately. Each leg records wall time, the peak
RSS of its whole process tree (sampled from `/proc`), LEC against the RTL
(gensim cell models, plus the bundle models for arm 3), a status class
(`ok`, `unproven`, `unsupported`, `infeasible` for a packing failure,
`timeout`, `failed`), coverage, area on both bases (rail inverters are
netlist cells and are counted once), `qor.place.multirow`/`multirow_eco`,
the stage period, classes and C_clk, HPWL and sizing counts. `--warm` reruns
each leg twice in its workdir and records the second. Output goes to the
ignored `repros/domplace_<date>/`: `manifest.json` (HEAD, dirty-diff hash,
lhd hash, input hashes, options, commands), `results.json`, `results.tsv` and
each leg's workdir, DEF/LEF and bundle. `--dry-run` writes only the manifest;
`scripts/domplace/arms_smoke.py` checks the plan and runs arms 1 and 3 on the
in-repo fixture. Final RUDY is not reported (plan H3 is not implemented).

Designs must map every retained register with the Liberty's register family
(the 7-cell sky130 fixture has no reset or negative-edge flop). HighTide
runs are not integrated: they wait on the decisions to patch `lhdsweep.py`
(scoring arm 3's OpenSTA timing as unmeasured, U14) and on the fixed-core
source (U15).
