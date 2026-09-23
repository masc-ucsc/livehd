# Unate synthesis

**Contents** — [Flow](#flow) · [Running it](#running-it) ·
[The search](#the-search) · [Optimizer](#the-optimizer-covers-rails-and-witnesses) ·
[Recovery](#bounded-recovery-local-joint-and-windowed) ·
[Image/divisors](#image-dependency-and-divisor-refinement) ·
[Encoding](#cofactor-encoding-synthesis) · [Reshaping](#associative-reshaping) ·
[Tech mapping](#technology-mapping) · [Verification](#verification) ·
[Reports](#reports) · [Reuse](#incremental-reuse-and-function-templates) ·
[Resources](#resources-and-isolated-workers)

Open questions, deferred work and the completion gates live in
[`todo/livehd/synth-unate.html`](../../todo/livehd/synth-unate.html); this file
is the directory's contract, not its task list.

## Flow

`pass.synth` is the unate mapper behind `lhd synth --set synth.mapper=synth`
(`synth_domino.md` U0). It reuses `pass.abc`'s region extraction, bit blasting,
state handling, cell readback and boundary sizing, and replaces only what maps a
region:

1. **Decompose.** The region's logic comes straight from `pass.abc`'s bit
   blaster (its blast tape: AND/OR/XOR/INV gates, constants folded), hashed
   structurally with complemented edges folded into each consumer's table. No
   ABC logic synthesis (no `strash`) runs before the decomposition; ABC only
   holds the region's PI/PO/latch skeleton for the stitched result. An XOR
   stays one 2-input source node when every recipe admits its 4-literal form,
   else it becomes three ANDs. The logic is decomposed into **unate
   functions**: each is a positive SOP over dual-rail inputs (`A`/`!A`,
   `B`/`!B`, ... are both available), within per-function limits on support,
   literal occurrences and series depth (a product term's length). Depth is
   unbounded by default (`max_depth=0`), so every cone decomposes; the
   objective is the **fewest unate functions** in the region (a demanded
   complement of an internal function is a separate twin function and counts).
2. **Technology-map.** ABC maps each unate function on its own
   (`strash; &get -n; &nf; &put -o`: technology mapping only, no
   restructuring across functions), and the fragments are stitched with one
   instance per function. The stitched region then gets `pass.abc`'s fanout
   buffering and gate sizing (`buffer -N`, `dnsize`) and the partition-boundary
   re-size: sizing only, never restructuring.
3. **Fallback.** A region the optimizer cannot handle (over `max_nodes`, a
   depth-capped recipe that finds no cover, a time/memory refusal, a failed
   function mapping) is mapped by the ordinary ABC flow, and its report row says
   why.

### Split mode (`--set pass.synth.split=true`)

An experimental alternative to the whole-region cover. A **gate** is one unate
function as above (at most `support` inputs, `literals`, `series`), and every
gate input -- a region input or another gate -- is available in either polarity
(a complement of a gate is one shared inverter cell, not a twin gate).

1. **Classify each output cone** by the fewest gates that build it: a cut
   dynamic program over the region, `cost(v) = 1 + sum(cost(internal leaves))`
   over the cheapest cut whose SOP fits in either output polarity, capped at 4.
   Buckets: `cones_wire` (driven by an input or a constant), `cones_2` (1-2
   gates), `cones_3`, `cones_more`.
2. A cone of at most 3 keeps all its gates. A larger cone keeps its **root gate
   and one gate under it**, both chosen to cover the most logic.
3. The logic under the kept gates that no gate computes is the **remainder**:
   rebuilt from the region inputs, cleaned up (hashed, folded, swept), then
   technology-mapped by ABC as one block with `strash; &get -n; &nf; &put -o`
   only (no `&dch`, `&fraig` or `dc2`) and checked by simulation.
4. Gates are mapped per function as in the default mode, and everything is
   stitched into the region.

Gate optimization and variable clustering (all on by default in split mode):

- `split_exact`: each gate's SOP is an exact minimum-literal cover (all primes
  within the series limit, then branch and bound), not the greedy cover.
- `split_factor`: the literal limit applies to an algebraic factoring of that
  SOP (kernel division, the series-parallel pull-down network), so for example
  `(a+b)(c+d)(e+f)` (24 SOP literals, 6 factored) is one gate.
- `split_cut` (default 10): a node whose best gate-sized cut costs 3 or more
  gates also tries cuts of up to this many leaves as `G(H(B), rest)`, with `H`
  a gate right after the bound set `B` of variables (column multiplicity at
  most 2). When `G` is not one gate it is split the same way into two gates.
- `split_share`: among valid bound sets, prefer an `H` another node already
  uses; identical local gates are emitted once and shared across cones.

Rows and totals report `cones_*`, `remainder_{outputs,nodes,cells,area,ms}`,
`split_inverters`, `bound_gates` and `shared_bound_gates`. A split region archives no unate witness (`lhd lec`
checks the stitched netlist), so `summarize.py` does not verify split runs.

Measured on lhdsuite dino (test.lib): area 516k vs 627k for the cover mode and
272k for the ABC flow; cones 10,606 wire, 6,185 in <=2 gates, 451 in 3, 4,405
more; the remainder is 78% of the area. The search work budget (`work`) is the
main limit on large regions: at the default 5M most nodes of the big regions
fall back to their own fan-in. With 50M, cones in <=2 gates rise to 7,026 and
the remainder shrinks, but the static-cell area grows (558k), because each gate
is technology-mapped on its own.

`pass.abc`'s ware trials (re-mapping arithmetic regions under alternative adder,
multiplier and barrel lowerings) are off for this mapper: a trial re-runs the
whole region and would re-decide it. There is no competition against an ABC
baseline and no equivalence proof inside synthesis; see
[Verification](#verification).

## Running it

Switch the mapper on the ordinary fused synthesis command; both paths use
`pass.color synth`, the same Liberty/options and OpenTimer reporting:

```sh
./bazel-bin/lhd/lhd synth design.v --top top --set synth.liberty=cells.lib --set synth.mapper=abc --workdir review/abc --emit verilog:review/abc.v --stats
./bazel-bin/lhd/lhd synth design.v --top top --set synth.liberty=cells.lib --set synth.mapper=synth --workdir review/synth --emit verilog:review/synth.v --stats
./bazel-bin/lhd/lhd lec --impl lg:review/synth/synth/net --ref lg:review/synth/synth/lg --lib lg:models --top top
```

Run these serially. `synth.mapper=synth` also selects the synth color profile
(`pass.color.synth.mapper=synth`: no `stop_*` cuts, so colors run register to
register). Existing `abc.*` mapping knobs apply to both entry points -- the
fused command and the standalone `lhd pass synth`; explicit `pass.synth.*`
settings override them (one helper, `merge_mapper_sets`, implements that
precedence for both). The standalone pass:

```sh
lhd pass synth lg:compiled --top top --set synth.liberty=cells.lib --emit-dir lg:mapped --workdir W
```

To compare the standalone passes on byte-identical copies of one colored input,
with process-tree memory/time guards and independent CVC5 checks of both outputs:

```sh
bazel build -c opt //lhd //pass/synth:measure_synth
python3 pass/synth/compare.py design.v --top top --liberty cells.lib --workdir review/passes \
  --synth-set support=4 --synth-set max_depth=0
```

The comparison directory must be new. It retains `comparison.json`, both Verilog
netlists, exact commands, logs, mapping measurements and complete OpenTimer
reports. The default guard is 120 seconds and 4096 MiB per command; override
with `--seconds` and `--memory-mb`.

## The search

One recipe bounds each unate function: `pass.synth.support` (logical inputs,
default 6, at most 12), `pass.synth.literals` (literal occurrences, default 16)
and `pass.synth.series` (literals per product term, default 4), plus
`pass.synth.max_depth` (function-network depth, default 0 = unbounded). Levels
are logical function dependencies and never introduce RTL pipeline stages.
`lhd list options pass.synth` lists every search and mapping option.

The structural search enumerates cuts of at most `support` leaves per node
(retaining `cuts` = 32 per node), forms both rails of each cut as a positive SOP
within the literal/series limits, and selects per rail by **area flow** (the
fanout-shared function-count estimate), with depth only as a tie-break. Cut
truth tables are simulated from per-thread scratch arrays, so a cut costs its
cone, not the region. A deterministic work budget (`pass.synth.work`, default
5,000,000 per region) bounds the search. With unbounded depth an exhausted
budget is not a failure: every remaining node takes its own fan-in cut (a
source node is a 2-input AND, or an XOR only when the recipes admit it, so it
is always formable), so the region still decomposes, with
more and smaller functions; the report counts these `fanin_fallbacks`. A
depth-capped recipe keeps the bounded contract instead: exhaustion yields no
network, and the residue engines below (divisor covers, cofactor encoding,
associative reshaping) try to cover what the cut search could not. Exhaustion is
never a proof of infeasibility.

At each partial cut merge, duplicate cuts are removed and the retained slots
cycle through size, estimated depth and estimated area-flow rankings. A cut
already retained by another ranking is skipped. One slot therefore keeps the
smallest cut, two also protect a shallow alternative, and three add area
diversity. The self cut is carried separately for parents. Depth and area
estimates use the cheaper available rail and fanout-weighted producer costs;
actual rail demand, depth and cost are checked after forming the function.
Retention remains heuristic and does not enumerate all cuts.

### The optimizer: covers, rails and witnesses

`unate.cpp` constructs total functions and prime covers, tracks both rail demands,
shrinks irrelevant support with a separating-cut witness, selects a shared DAG,
and verifies the witness against the source network. Internal negative rails have
independently synthesized twins. Source inversion has an explicit cell-producing
node, counted separately from logical function levels. No complemented edges
remain in the exported DAG.

After the initial area-flow selection, bounded shared recovery tries retained
cut/form alternatives for each demanded rail. It recounts the complete
dependency closure, charging every unate function (twins included) once; a
source rail is a free input, so source inverters cost nothing here.
The structural objective is lexicographic: unate functions, total literal
occurrences, then total ports. Only strict improvements are accepted; ties keep
the incumbent. Each accepted replacement recomputes actual dependency depth and
passes the independent witness verifier within the same recipe limits.

### Bounded recovery: local, joint and windowed

`pass.synth.recovery_rounds` defaults to two deterministic sweeps (0 disables;
maximum eight). Recovery shares the recipe sequence's work/resource budgets.
Exhaustion retains the verified initial network and latest verified recovery.
Both are independently mapped and compared against ABC, because a structural
improvement need not improve physical QoR. These are local improvements over
retained structural candidates, not a global optimality result or functional
decomposition search.

`pass.synth.joint_limit` adds exhaustive joint selection for small retained-choice
spaces (default 256 combinations; 0 disables; maximum 65,536). It follows every
alternative's rail dependencies, including producers absent from the current
network, and counts the Cartesian product before enumeration. If that product
exceeds the limit, `pass.synth.joint_windows` admits bounded related-choice
windows (default 8; maximum 256; 0 retains `candidate_limit` and skips this
fallback). Windows prioritize decisions that may share a rail or depend on one
another, considering all retained alternatives, including currently unused
producers. Each window's complete Cartesian product must fit `joint_limit`;
individual variables' alternatives are never truncated. Outside choices remain
at the latest verified incumbent, and every combination recounts the complete
shared network. Windows share the search work/resource budget.

Every combination rebuilds the complete shared dependency closure and checks
actual logical depth. Strict cost improvements require independent witness
verification. `joint.complete=true` means minimum structural cost over this
recipe's retained-choice closure only, including both rail costs. It does not
establish optimal physical QoR or optimality among unenumerated cuts, forms,
functional decompositions or don't-care completions. Interrupted enumeration
keeps its last verified improvement and reports no optimum. Initial, locally
recovered and jointly recovered candidates are all retained for mapping.
Window recovery reports `joint.scope=bounded_windows`, the number of admitted
and completed windows, and aggregate combinations/checks. It always reports
`joint.complete=false`: fully checking individual neighborhoods does not prove
a global optimum or a fixed point after later windows change outside choices.
`window_limit` means the configured window count was reached; interrupted
windows retain only verified incumbents. The window count also participates in
persistent recipe identity and exported witness search metadata.
Set `recovery_rounds=0`, `joint_limit=0` and `divisor_limit=0` to disable all
post-cover recovery.

### Image, dependency and divisor refinement

`pass.synth.image_inputs` enables structural image and existing-divisor refinements and sets their
exhaustive enumeration threshold (default 8 original sources, maximum 12;
0 disables both enumeration and symbolic queries for those refinements).
New encoding synthesis has separate admission bounds and mandatory reconstruction
proofs, described below; use `encoding_limit=0` to disable it. For each
separating cut with internal leaves, the optimizer reconstructs the leaves'
original source closure and enumerates reachable tuples below this threshold.

Larger cones use `pass.synth.symbolic_nodes` (default 4096 nonterminal nodes per
query, maximum 65536, 0 disables symbolic queries). `relation.cpp` builds reduced
ordered binary decision diagrams over original sources, composes each original
truth table using exact if-then-else operations, and tests each divisor tuple's
nonempty source-space block. A dependency holds only when the original root has
one value throughout every reachable block. Nonempty true and false subsets
refute that proposal. The diagram never treats a missing sample as unreachable.
There is a separate admission bound of 256 original sources, bounding recursion;
the support/result tables still admit at most 12 divisor inputs.

Unique nodes and apply-cache entries are bounded (the latter at four times the
node bound); recursive operations share the search work/resource budget. Node
or source admission refusal retains total care for structural choices and skips
unproved dependency proposals. Work exhaustion stops search without invalidating
an earlier verified incumbent. Limits never establish infeasibility. No partial
image/dependency result can authorize a completion or a recovered function.

The partial-function SOP builder merges onset and don't-care minterms, covers
only the required onset and freezes the resulting form into a **total**
completion. Support shrinking, rail demand and tmap use that specific completion.
Total-care alternatives remain available alongside these candidates. Positive
and negative twins may choose different off-image completions; both must agree
with the original function on the proved image. Recovery considers their
actual producer and literal costs and rechecks the entire chosen network.
This is bounded completion search, not proof of an optimal completion or an
infeasibility certificate when no form is found.

Each image-aware function exports its exact care mask in original separating-cut
order and the original source IDs used to prove it. Both production verification
and the independent Python checker recompute that image, check source equality
on it, and check the form against its total completion on **all** assignments.
The archive also retains the image bound; the checker continues to accept
schema-1 total-care, schema-2 image-care and schema-3 functional-dependency
archives. Per-attempt `image` statistics report queries,
proved images, admission refusals, admitted candidates and image-aware
functions in that variant. `symbolic` counts queries routed through the diagram
engine, including interrupted/refused ones. `cover` and `divisors` additionally
separate `symbolic_limited` allocation refusals from `source_limited` admission
refusals. Both bounds participate in the persistent recipe key and witness search
metadata. The independent Python verifier uses its own bounded Boolean AND/OR
and complement implementation for closures above 12 sources, checking exact
care, dependency constancy and source closure without trusting producer metadata.
Its own resource limits return an inconclusive result, never a pass.

`pass.synth.divisor_limit` enables recovery through producers outside a
function's structural fan-in (default 64 proposed sets per demanded function;
maximum 4096; 0 disables). It runs after the initial cover and local/joint
recovery have produced a verified incumbent. The deterministic candidate pool
contains up to eight preceding source/producer identities from that incumbent,
in descending source-network ID order. Proposals include the empty set
(constants), singletons, pairs and single-variable replacements in the current
support; they retain the recipe's support bound.

For each ordered divisor set, the dependency query evaluates the union of its
source cone and the original root's cone, using the same enumeration threshold
and symbolic-query bounds. A dependency is proved only if every original-source assignment with the
same divisor tuple gives the same root value. A conflicting pair refutes that
specific proposal; source/work limits are distinct inconclusive outcomes.
The resulting exact care and onset yield a total completion with explicit rail
demand. Every proposed rewrite rebuilds the complete shared network, charges
reactivated producers and twins, enforces actual depth and proves the full
Boolean witness before retaining a strict structural-cost improvement.

The `divisor` variant is mapped and compared physically alongside initial,
local and joint variants. `divisors` reports proposal queries, refutations,
source-limit refusals, whole-network checks, improvements and exhaustion.
Schema-3 witnesses mark functional dependencies explicitly; both verifiers
reconstruct their source-space partitions instead of pretending the divisors
are separating cuts. The option participates in the netlist cache key and
witness metadata. Incomplete search retains verified incumbents and claims no
optimum.

`pass.synth.cover_limit` separately rescues outputs without a structural cover
(default 32 proposed divisor sets per unique uncovered output, maximum 4096,
0 disables). Before emitting the initial network, it searches up to eight
preceding source/producer identities with a realizable rail shallower than the
recipe's depth bound. The same dependency queries and proposal forms apply,
using the output's original inputs for single-variable replacements. It chooses
by level then area flow, retains alternatives for subsequent local/joint
recovery, and proves the complete emitted network before returning a feasible
attempt. A candidate discovered before exhaustion is never a publishable
incumbent until that proof finishes. Repeated outputs share the same root.

The attempt's `cover` object reports queries, refutations, source-limit refusals,
recovered roots, exhaustion and the outcome. `roots` counts roots given candidate
choices; it does not certify a complete verified network (use attempt status).
The option is part of the netlist cache key and witness metadata. A four-input
AND-chain regression that fails a two-level, two-input structural recipe succeeds
by reusing a separate `c & d` producer. The CLI regression similarly recovers
`(a & c & d) | (b & c & d)` using separate `a | b` and `c & d` outputs;
it checks physical mapping, a separate `lhd lec`, independent witness checking,
warm replay and cache invalidation when the recovery bound changes.
This stage searches existing functions. New functions are generated by the
separate cofactor engine below. Symbolic queries support larger original-source
cones within the explicit bounds above; richer completion search remains pending.

### Cofactor encoding synthesis

`pass.synth.encoding_limit` enables shared cofactor encoding synthesis after a
recipe cannot produce a complete structural/divisor cover (default 16 proposed
bound sets per recipe; maximum 4096; 0 disables). It admits at most 12 original
sources across eight unique non-source outputs, and requires at least two
logical levels. Deterministic bound sets contain at most the recipe's support
limit; larger sets are tried first, with ascending source-mask order as the tie
break. The remaining free sources must also fit the support limit.

If single-set search is incomplete, `pass.synth.encoding_pair_limit` admits
additional pairs of disjoint bound sets (default 16 pairs per recipe, maximum
4096; 0 disables pairs). `encoding_limit=0` disables the entire encoding engine.
Pairs are enumerated once: descending first-set size, ascending first mask, then
descending submasks of its complement, retaining only pairs whose first mask is
smaller. Each set has at most the recipe's support limit. Matrix construction and
proposal enumeration share the existing work/resource budget.

For each bound assignment, the engine computes a vector of cofactors across
**all** admitted outputs. Identical vectors form one class. First-seen class IDs
provide the initial shared Boolean encoder bits on the bound sources. The
information count `ceil(log2(classes))` only admits a proposal: each decoder's encoder bits plus
free sources must fit the support bound, and every demanded encoder rail and
decoder must pass the literal, series and actual-depth checks. Both polarities
are distinct charged producers; repeated outputs and shared encoder readers
reuse their producer. Decoder care is proved exactly, including unreachable
class codes, and each selected completion is frozen into a total function.
For a pair, each set's classes are computed against all remaining sources,
including the other set. Thus every class is interchangeable in every context;
the decoder uses the combined encoder bits plus any unbound sources. Exact
dependency queries independently establish that these combined divisors suffice.

`pass.synth.encoding_code_limit` bounds injective class-to-code assignments per
admitted single-set or paired-set proposal (default 8, range 1..4096). One keeps
the discovery-order assignment. Larger bounds enumerate assignments
lexicographically at the minimum bit width, including previously unused codes;
unassigned codes are not permuted, so equivalent assignments are not repeated.
For paired sets the last set varies fastest. Classes and their cofactor matrices
are reused across trials; encoder functions, decoder care and total completions
are rebuilt and proved for each assignment under the shared work/resource budget.
For example, three classes encoded as `00,01,10` may require a costly
nonuniform-input detector, while `00,01,11` permits simple OR/AND encoders.
The first fully verified feasible assignment wins; this does not optimize the
physical QoR across all class assignments.

The engine independently proves every decoder dependency and then verifies the
complete network against the original outputs before publishing a candidate.
An interrupted final proof publishes no incumbent. Invalid reconstruction is an
optimizer error; proposal/source/output/work refusal is inconclusive and retains
the region's ABC fallback. The new candidate is then technology-mapped like any
other decomposition.

Schema-4 archives retain total encoder definitions on original sources under
`network.encodings`; they do not change the original source digest or introduce
new primary inputs. Both verifiers reconstruct these definitions and prove the
result against the original output functions. The independent checker also
accepts older schemas. Attempt `encoding` counters report single-set `queries`,
`pair_queries`, `code_queries` (evaluated assignments summed across proposals),
`code_limited` (any proposal stopped with assignments remaining), successful
`bound_sets`, `classes` (the product of class counts for a pair), total `bits` and
the outcome. All three search bounds participate in cache identity and witness
metadata. Tests cover a new shared parity encoder with both rails,
two independent parity encoders that cannot fit any single bound set,
three classes requiring two bits and unused-code care, zero-bit decompositions,
corrupt definitions, proposal/work limits, mapped proof and cold/warm cache reuse.

This is a bounded first-feasible search over one or two disjoint bound sets for
the admitted output group. It does not enumerate wider-than-minimal encodings,
overlapping sets, more than two sets or larger source groups, and makes no
optimality or infeasibility claim.

### Associative reshaping

`pass.synth.reshape_limit` enables associative depth regrouping after structural,
divisor and cofactor-encoding search fail (default 32 unique non-source output
roots, maximum 4096; 0 disables). It recognizes AND/OR cones with signed source
literals and optional output complementation, including constants and repeated
literals. It normalizes each output to a conjunction of signed original sources
with an optional complement, then partitions that conjunction into deterministic
source-ID-ordered groups. If one grouping layer cannot fit the decoder support,
it recursively groups the resulting signals up to the recipe's logical depth.
Identical groups share encoder definitions across
outputs; demanded positive and negative rails remain separately charged.

This stage requires at least two logical levels. Each group and decoder obeys
the recipe's support, literal and series bounds; the recognition bound is
`min(256, k**L)` source literals per intermediate expression. At most 256 new
encoder definitions are admitted across all outputs. All non-source outputs must be recognized for a
candidate to be emitted. The grouping width accounts for positive conjunction
series depth when both output polarities are demanded. Rail demand is propagated
backward through the definition hierarchy, then each demanded rail is emitted
once in producer order with its frozen total form. It tries one deterministic
partition at each layer, not all partitions or arbitrary Boolean balancing.

Every decoder is checked against its original output using exact dependency
queries (enumeration through 12 original sources, then the configured bounded
ROBDD engine). The fixed total AND/NAND completion is checked on every reachable
divisor code; final whole-network witness verification is mandatory. Work and
symbolic limits return an inconclusive search result without a partial incumbent.
Schema-5 witnesses allow encoder definitions to read original primary sources or
earlier encoder definitions. Both verifiers reject forward/cyclic references,
references to original internal logic and more than 256 definitions; each emitted
function and its actual depth are checked before proving the original outputs.
Flat archives with at most 12 definitions retain schema 4 for compatibility.
No encoder is treated as a new primary input. Attempt `reshape` reports
successfully verified root/group counts and the outcome; `reshape_limit` is
included in reports, witness metadata and cache
identity. Tests cover two-level 16-source reductions with exhaustive evaluation,
and signed 27-/28-source reductions requiring three/four levels with exact proofs,
satisfying/near-miss assignments and random samples. A serial 16-source reduction
with two-input functions needs four levels (`max_depth=4`) and succeeds through
mapping, a separate `lhd lec`, independent checking and cache reuse.
Reshaping beyond associative cones and partition exploration remain future work.

### Technology mapping

`abc_tmap.cpp` maps each unate function on its own, for area and without a
timing environment: `strash; &get -n; &nf; &put -o` on the function's SOP. Every
fragment is read back into cells and checked against its function exhaustively
(both SOP descriptions evaluated on all assignments of at most 12 inputs; wider
functions use a SAT miter). This is a unit check of the cell readback (pin order,
polarity), not an equivalence proof of the design. `tmap.cpp` stitches one
fragment per selected DAG node, retaining ownership and sharing; source rails
become inverters where a complement is demanded.

Because functions map untimed, identical functions (AND2, OR3, ...) share one
mapping: the `Template_cache` is always on in memory for the invocation and is
persisted as `W/synth_cache_templates.bin` under the `lhd.incremental` switch.
Timing is handled at region level: the stitched region gets `pass.abc`'s
`buffer -N`/`dnsize` tail and the partition-boundary re-size.

Among the verified variants of a recipe (initial, local recovery, joint
recovery, divisor recovery) the one with the lowest structural cost (functions,
then literals, then ports) is mapped; only that one reaches ABC.

## Verification

Synthesis does not prove equivalence. Check a mapped netlist with `lhd lec`
(generated Liberty models via `lhd pass liberty gensim`), as `compare.py` and the
tests do. Inside the pass the optimizer still verifies each decomposition
against its source network over the cut truth tables (`verify`, cheap and
structural) and each tmap fragment against its function; a mismatch in either is
an optimizer defect and a hard error.

`qor.json.witness.jsonl` archives the source network of every searched region
and the selected decomposition. The independent checkers re-derive them:

```sh
python3 pass/synth/check_witness.py W/synth/qor.json.witness.jsonl
python3 pass/synth/summarize.py W/synth/qor.json.synth.json W/synth/qor.json.witness.jsonl [--invocation-result W/result.json]
```

`summarize.py` checks every archived source and decomposition, binds each unate
region to the decomposition it selected, and recounts that region's functions,
negative-rail functions (`twins`), literals, ports and source inverters from the
witness. It does not rerun mapping, LEC or timing; physical and resource figures
stay reported, not independently verified. A truncated archive
(`pass.synth.witness_bytes`) makes the evaluation `incomplete`, never verified.

## Reports

With a workdir, `qor.json.synth.json` (schema 2) accompanies the usual mapping
report (under `W/synth/` for the fused command; the fused result carries it as
`qor.synth`, and `--emit-dir report:` exports it). It records the recipe, totals
(regions, unate, abc_fallback, reused, functions, twins, literals, search and
tmap time) and one row per searched region: `status` (`unate` or
`abc_fallback`), the fallback `reason`, the selected `variant`, function/twin/
literal/port/source-inverter counts, depth, maximum support/literals/series,
mapped area and delay, and per-recipe `attempts` with the search engines'
statistics (fan-in fallbacks, recovery, joint, encoding, reshape, image, cover,
divisors). `regions_reused` wraps cached decisions with
`metrics_scope: historical_search`.

## Incremental reuse and function templates

Persistent netlist reuse uses `W/synth_cache` under the single `lhd.incremental`
switch; the region key includes the search recipe and source salt, so the two
mappers never share rows. A cached region carries its decision and witnesses as
a bounded attachment (64 MiB per row); missing, truncated, corrupt or
inconsistent evidence forces a region miss and a fresh search. A hit replays the
witnesses into this invocation's archive (`reused_from` keeps the original
region and record). Function templates persist separately (above).

## Resources and isolated workers

Resource admission carries the original ABC region's elapsed time and entry
footprint into the unate search; `pass.synth.time_budget_ms`, `memory_budget_mb`
and `allow_oversize` keep their ABC meanings. Admission samples the process
(a syscall), so the search checks it every 262,144 work units and the region
re-samples at most every 2 ms. When `pass.synth.time_budget_ms` is positive,
uncached function mapping runs in an isolated self-exec `lhd` worker whose
allowance is the region's remaining time; the parent can kill and reap it during
a blocking ABC call. With no time budget, mapping stays in process. Mapping is
serialized (one region at a time), including all per-function calls.

For enclosing command costs, save the CLI envelope with `--result-json PATH`;
`synthesis_invocation` reports the parent's wall time and peak RSS. For external
process-tree measurement, launch one command at a time under
`bazel-bin/pass/synth/measure_synth --archive DIR --seconds N --memory-mb M -- <command>`.
