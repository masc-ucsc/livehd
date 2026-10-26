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

Arms 1 and 2 run today; they may keep STA on for report-only timing, and
`resize=false` keeps STA from enabling repair. `io_resynth=0` explicitly
disables the placement resynthesis loop. Arm 3 becomes available after
emission (D) and two-row legalization (F) land.
