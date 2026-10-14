# Optimizing synthesis from Pyrope

Improve area and frequency of a Pyrope design under `lhd synth` without
changing its function. There are two levers: **synthesis attributes**
(semantics-free hints on the source) and **source rewrites** (an equivalent,
cheaper formulation, proven with LEC). Run-wide `--set` options are for
exploration; a win that should last becomes an attribute in the source.

## Attributes

An attribute never changes what the hardware computes; it steers partitioning
(which cells form one mapping region) and how each region is mapped. Spelling:

```pyrope
pub mod core::[synth.partition="coarse", synth.abc_pre="dc2"](...) -> (...) { ... }   // module
pub comb alu::[synth.adder="cla"](a:U64, b:U64, op:U4) -> (r:U64) { ... }
{::[synth.color="lane0", synth.adder="brent"] x = (a + b)#[0..<16] }                  // block
const next::[synth.adder="prefix"] = count + 1                                        // one declaration
{::[synth=(color="lane", grow=false)] ... }                                           // tuple form
reg rf:[32]U64:[synth.memory="flops"] = 0                                             // one memory
```

| key | values | applies to |
| --- | --- | --- |
| `adder` | `auto` `rca` `cska` `cla` `prefix` `brent` | Sum, compares, Mult/Div internals |
| `block_size` | integer (0 = derived) | CSKA/CLA block width |
| `multiplier` | `auto` `array` `tree` `csa` `tdm` | Mult |
| `barrel` | `auto` `log` `reverse` | runtime shifters |
| `partition` | `fine` `coarse` | the scope's region granularity |
| `color` | name or integer | explicit group: these cells form one region |
| `grow` | `false` | an explicit group does not absorb neighbouring logic |
| `stop_mux` `stop_arith` `stop_cmp` `stop_shift` | `true` `false` | cut regions at that operator family |
| `delay` | ps | the region's timing budget |
| `abc_pre` | `;`-separated allow-listed ABC steps | optimization before the region's mapper (abc mapper only) |
| `abc` | ABC script | the region's whole flow (overrides `abc_pre`) |
| `ware` | `true` `false` | explore arithmetic/shift modules in the region |
| `memory` | `auto` `flops` `macro` | realization of one memory |

Rules:

* `lhd describe synth.<key>` documents each key. Each has a same-named run-wide
  option (`--set synth.adder=cla`) except the source-only ones (`color`,
  `grow`, `ware`, `abc`). A source attribute overrides the run-wide value, and
  an inner scope overrides an outer one, so a `--set` does not move a pinned
  module.
* An operation-specific key only affects cells it applies to (`adder` on a
  Sum, not on a Mux). Attributes ride through inlining of the attributed
  `comb`.
* Region-level keys (`delay`, `ware`, `abc`, `abc_pre`) describe one mapping
  run. All cells of one region must agree on them; a disagreement is a compile
  error pointing at the second pin. A hierarchy that is merged into one region
  therefore needs the same value on every module.
* An explicit group nested inside another explicit group cuts the outer one
  and usually costs QoR (it warns). An attribute that lands on no cell warns.
* The `usyn` mapper ignores `abc`/`abc_pre` with a warning, so one source builds
  under both mappers.

## Method

1. **Fix the measurement.** One Liberty, one target period, memories black-boxed
   (`--set pass.abc.memory=false`) unless memory realization is the question.
   Always record cells, area and STA delay together, and the mapper used.
   Pick a period the design misses (or a realistic SDC); with an unreachable
   target, delay-driven choices degenerate.
2. **Find the critical path before touching anything** (see `lhd.md`):
   ```sh
   lhd synth top.prp --top top --set synth.liberty=cells.lib --emit-dir lg:net --workdir W
   lhd pass opentimer lg:net cells.lib top.sdc --top top.top --set pass.opentimer.critical=true
   lhd tool sta paths=5 lg:net --top top.top
   ```
   The start and end state names say which logic is on the path; the attached
   source lines say which declarations own it. Decide whether it is an adder,
   a shifter, a mux tree, a decoder, or fanout.
3. **Choose the lever that matches the path** (next section) and apply it to
   the narrowest scope that covers the path: a declaration beats a block,
   which beats a module.
4. **Measure the variants, not one run.** Mapping is heuristic and not
   monotonic: a few percent either way is noise. Compare several variants on
   the same setup and keep the best only when it clearly wins. If the source is
   shared by both mappers, check both and reject a pin that makes the other
   noticeably worse.
5. **Prove it.** Attributes are semantics-free by construction, but run LEC of
   the mapped netlist against the source anyway (it catches compiler bugs).
   Source rewrites must prove against the previous version. `TIMEOUT-time` /
   `TIMEOUT-memory` prove nothing (back them with simulation); `REFUTED-cex` is
   a bug to find, never a result; any other `REFUTED-<why>` means the check
   did not reach an answer (fix the setup, or report `REFUTED-unexplained`).
6. **Record** before/after (cells, area, delay) and the reason for each pin, in
   a comment next to it when it is not obvious.

A useful acceptance rule: keep a change when area x delay improves and neither
term regresses beyond what the design's goal tolerates (for example area
<= 0.95x at delay <= 1.05x for an area pass).

## Levers, by what is on the path

**Region granularity (try first).** Partitioning is usually the largest lever.
`fine` cuts regions at wide arithmetic, shifters, compares and muxes and maps
them separately; `coarse` keeps register-to-register cones whole so the mapper
can optimize across them. Control-heavy and CPU-like modules typically gain a
lot from `synth.partition="coarse"` (large area and delay reductions are
common); small arithmetic blocks can go either way. One explicit `synth.color`
on a module's top merges its logic into one region with a similar effect. Do
not nest colors inside a colored module.

**A wide adder or comparator.** Try `synth.adder` on the declaration that owns
it: `cla` is a cheap improvement over ripple; `prefix`/`brent` are fastest and
cost area. Pin only the critical adder: a module-wide `prefix` can cost far
more area than it saves in delay, while pinning the one counter or address
adder on the path gets most of the gain. If the adder choice barely moves the
delay, the adder is not the limit; look at what feeds it (bypass muxes,
select decoding).

**Multipliers and shifters.** The default multiplier is usually best;
`array`/`tree`/`csa` tend to cost area. `synth.barrel` helps only when a
runtime shifter is on the path.

**Technology-independent clean-up.** `synth.abc_pre` runs extra ABC passes
before mapping (`dc2`, `resyn2`, `compress2rs`, `&dch`, `&synch2`, ...). No step
helps on average, but the best of a small sweep often wins a few percent on a
given design, and the winner differs by design. Sweep 5-7 candidates run-wide
(`--set synth.abc_pre=...`), then pin the winner. Steps that restructure
heavily (`&synch2`) can lose badly on some designs.

**Fanout and clocks.** A high-fanout select or enable shows up as long buffer
chains on the path; restructure (duplicate the select, register it) rather
than look for an attribute. Gated clocks get a buffer tree automatically; if a
clock net still dominates, check the clock-gating structure.

**Memories.** Know how each memory is realized (the result JSON lists every
memory and why). Small register arrays with many ports may become flops plus
decode, which can dominate area; pin `synth.memory="macro"` or `"flops"` on
the declaration to decide explicitly.

## Source rewrites that pay

Equivalent formulations that map much better (prove each with LEC):

* Lowest-set-bit isolation `v & -v` builds a full negation: a prefix-OR scan
  of the lower bits (bit `i` is set when `v#[i]` is 1 and no lower bit is) is
  smaller and faster.
* Selecting a slice with a wide variable shift (`(data >> (sel*W))#[0..<W]`)
  builds a full barrel shifter: an AND-OR over the decoded select (or an array
  read) is about half the area.
* A second shifter computing a mask (`(1 << n) - 1` next to `x << n`) can be a
  thermometer code of `n`.
* A priority `if/elif` chain over mutually exclusive conditions can be a
  one-hot AND-OR select (`unique if`) when exclusivity really holds.
* Operator sharing across exclusive arms (one adder for add/sub/compare, one
  shifter for all shift kinds) saves area but can lengthen the path; measure
  both before keeping it.
* A long counter or address increment on the critical path: pin a faster
  adder on just that declaration
  (`const next::[synth.adder="prefix"] = count + 1`).

## Pitfalls

* Run-wide `--set` values do not override source pins; when an experiment
  shows no change, check whether the module already pins the key.
* Merging many modules into one region can defeat name-based LEC state
  pairing; lec then falls back to a flat proof that may end in `TIMEOUT-time`
  on a large design. Check per module or pair state explicitly.
* An attribute that disappears in an optimization (operator merging, inlining)
  should warn; if a pin seems to have no effect, confirm with a run-wide
  `--set` of the same value that the result moves.
* Annotate the Pyrope source, not generated Verilog; regenerate derived
  artifacts after the change.
