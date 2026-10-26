# pass/domplace — provisional domino placement

Post-freeze code for [2d-domplace](../../todo/livehd/2d-domplace.html): it turns
USYN's frozen one-phase selection into a mixed CMOS/domino physical netlist
with provisional cell models. Everything here is opt-in. The logical
selection, its artifacts and the logical region cache are unchanged: the
logical target stays `cmos`, and this package sits outside `//pass/usyn:usyn_salt`
so that editing it never misses a logical cache entry. `//pass/domplace:domplace_salt`
keys its products instead.

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
| Positive edge, one-bit clock driven directly by a region input in the selected domain | candidate | — |
| Enable or synchronous reset (already folded into D) | candidate | — |
| Asynchronous reset | retained | `async-reset` |
| Power-on initial value | retained | `initial-value` |
| Negative edge | retained | `negative-edge` |
| Unknown edge | retained | `unknown-edge` |
| ICG-gated clock (the clock root is a recognized clock gate) | retained | `gated-clock` |
| Clock control is not a direct region input | retained | `derived-clock` |
| Multibit clock / constant or absent clock | retained | `multibit-clock` / `constant-clock` |
| Clock port is not `domino_clock` | retained | `clock-domain` |
| Memory or opaque scope, transparent latch, ICG latch | retained (barrier) | `memory` / `opaque` / `transparent-latch` / `icg` |
| One-literal endpoint | retained unless `domino_identity=true` | `identity-endpoint` |
| No support after cofactoring | always retained | `constant-endpoint` |
| No positive-output formula within stack, branches and inputs | retained | `polarity-illegal` |
| CMOS+DFF estimate not larger (`domino_select=area`) | retained | `area` |
| CMOS cone could not be priced | retained | `cmos-estimate` |
| Physical selection work exhausted | retained | `selection-budget` |

A converted state carries `domino-area` or `domino-all`. Under `select=area`
the comparison is an area proxy: the domino side is `x * A_NOT + A_register`
plus one `A_NOT` for every complemented rail, and the CMOS side is the
register plus the Liberty estimate of the D-cone logic that only this state
uses. A tie retains CMOS.

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

The bundle sidecar (`domino_models.json`, schema 1) records the stage model,
units (`ps`, `fF`, `um`, DBU), the corner, technology and row-access identity,
the named reference inverter and register family, FO4 as a labeled estimate,
every master (formula, rails, metrics, variants with estimated and rounded
areas, pin caps, access checks) and every instance with the original state it
owns. Unset coefficients are `null` with a diagnostic.

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
written in every register-ladder variant; sizing is disabled, so the netlist
instantiates variant 0. An abstract that fails the pin-access check refuses
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

## Matched commands

All three arms pin the same placement knobs:

```
COMMON="--set synth.liberty=L --set synth.lef=T,C --set synth.sdc=S --set synth.place=true
        --set synth.place.timing_driven=false --set synth.place.resize=false
        --set synth.place.resynth=false --set synth.place.io_resynth=0 --seed N"
arm1 (ABC CMOS):  lhd synth top.v --top T --set synth.mapper=abc $COMMON --workdir W1
arm2 (USYN CMOS): lhd synth top.v --top T --set synth.mapper=usyn --set pass.usyn.clock_phases=1
                  --set pass.usyn.tmap=abc $COMMON --workdir W2
arm3 (domino):    arm2 + --set pass.usyn.physical=domino_provisional --set synth.opentimer=false --workdir W3
```

Arms 1 and 2 may keep STA on for report-only timing, and `resize=false` keeps
STA from enabling repair. `io_resynth=0` explicitly disables the placement
resynthesis loop. Arm 3 needs `synth.opentimer=false` (the domino cells have no
Liberty timing) and a Liberty whose register family maps every retained
register (the 7-cell sky130 test fixture has no reset or negative-edge flop).
Its converted states are two-row CORE cells, anchored before terminal
legalization (`pass/place/multirow.hpp`); `qor.place.multirow` reports them.
`lhd/tests/lhd_domplace_smoke.py` runs arm 3 on the fixture: legal placement,
2H domino components on whole rows in their start row's orientation, the
domino LEF in `lefs.json`, sequential LEC of the placed netlist, and a
deterministic DEF.
