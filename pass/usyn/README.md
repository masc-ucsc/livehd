# pass/usyn — register-rooted synthesis

`pass.usyn` builds a native XAG, selects register-rooted dual-rail functions,
optimizes their shared residual logic, and emits a complete logical CMOS design.
It retains the original register semantics. The optional ABC provider only
technology-maps that selected network; USYN does not call ABC optimization,
compare ABC candidates, or fall back to ABC synthesis.

```
lhd synth design.v --top top --set synth.mapper=usyn --set pass.usyn.tmap=none --workdir W
lhd synth design.v --top top --set synth.mapper=usyn --set synth.liberty=cells.lib --workdir W
lhd pass usyn lg:source --top top --set pass.usyn.tmap=none --emit-dir lg:net --workdir W
```

`tmap=none` needs neither Liberty nor an ABC provider. Fused synthesis skips
OpenTimer in that mode; explicitly requesting STA is an error. The default
`tmap=abc` requires Liberty and the registered optional provider, and maps the
whole selected logical network. An unavailable provider produces a diagnostic.

`synth.mapper=usyn` selects the register-to-register USYN coloring profile.
Configure the pass with `pass.usyn.*`; ABC options are not inherited.
Equivalence is checked separately with `lhd lec`; synthesis invokes no prover.

## Register-rooted redesign in progress

Remaining work is tracked in [2u-usyn](../../todo/livehd/2u-usyn.html). The
public entry now uses the native CMOS design driver and optional tmap interface. `pass.satopt` is off by default for every command;
explicit user opt-in is preserved, and USYN does not enable it.

`//pass/usyn:usyn_logic` is independent of ABC, graph libraries and solvers.
`xag` supplies structurally hashed AND/XOR graphs, bounded whole-cone and
reconvergence-driven windows, and 64-assignment-batch simulation through
16 logical inputs. `function` supplies exact support/polarity analysis,
monotone covers and phase-specific care-set completion, divisor-image
dependence, and bounded signed-cover/SP factoring. A gate is constrained by
logical inputs, series stack and factored parallel width; transistor count
is a cost, not a legality ceiling. Both polarities can feed the same gate.
Formula results carry explicit rail/output polarity and are checked against
their logical truth tables. Failed heuristic search is not infeasibility.
Signed covers undergo bounded redundant-cube removal before factoring, so a
redundant consensus term need not inflate the physical branch count. This
optional step reserves factoring work and retains the original valid cover on
refusal.

`endpoint` selects a named mandatory latch endpoint: admitted whole-cone
one-cell search, functional/existing-divisor parallel two-phase search, then bounded
selective removal into upstream CMOS. It retains an identity endpoint when
better absorption is unavailable, charges logic retained by outside readers,
and accounts for free DOMINO rails versus shared static inverters. Per-tier
work reservations keep two-phase exploration from exhausting the removal
budget. The provisional proxy weights are AND=2, XOR=4, NOT=1, plus 5 per
Domino or 8 per DominoLatch on top of its formula transistor count; these
are configurable estimates, not characterized cell area. Exact bounded
validation checks function composition and phase use.

`decomposition` generates new divisors from one bound-variable group B:
`F(B,R) = H(G0(B), ..., Gk(B), R)`. Bound assignments with identical cofactors
over R share a binary code; each code bit becomes a candidate first-phase
function. The endpoint search factors every function under the gate limits,
checks their composition on the original graph cut, and compares shared cost.
The default admits 32 partitions under a reserved work budget; zero disables
this search. This can expose `b | c` in `(a & b) | (a & c)` even when that
divisor has no graph node, and can produce multiple parallel first-phase cells.
Partition discovery follows deterministic mask order with one encoding.
Single-divisor candidates (two cells total) are realized first. Wider candidates
retain their computed tables and reachable-code relations for later realization;
a competitive two-cell fast result skips their factoring. Discovery/single-divisor
work consumes at most half this tier's remaining allowance, reserving work for
queued parallel candidates. Table generation and deferred realization share the
original per-candidate cap. Existing-divisor two-cell candidates run before
queued wider functional realizations, followed by unrestricted structural
candidates. The four stages share the two-phase work cap and retain work for
selective removal. Care completion is checked after unused producers are removed,
so a wider proposed boundary may still yield an accepted two-cell result.
The deferred queue can use at most half the boundary-memory allowance; boundary
enumeration uses the unoccupied remainder while both are live. The queue is
released before the unrestricted structural sweep. Storage refusal preserves
the legal incumbent; the temporary two-cell search limit is not a topology cap.
The top function records its reachable code image. After trying zero fill, bounded
care search tries one fill and the least/greatest signed-monotone completions
for selected input polarities. `care_phases=16` limits these polarity trials
(0 disables, maximum 64); these are independent of clock phases. Each top
alternative is checked by composition and scored with its complete mixed
network. Work reservations preserve the incumbent on exhaustion. Alternative
encodings, multiple independent bound groups and more general joint endpoint
optimization remain pending; this search does
not establish an optimal decomposition or completion.

Existing cut boundaries also derive their reachable image over the admitted
independent basis. Care completion can select two-phase absorption, a single
endpoint fed by correlated static signals, or a mixture of static and produced
inputs. A completion that removes an input also removes its unused proposed
first-phase cell, including an unrealizable proposal. Validation independently
rederives correlated static inputs on the same basis and rejects dependencies
outside the admitted cone, even if they happen to compute an equivalent value.
Shared outside readers retain their original static logic and deletion cost.
Reports count existing-boundary care attempts and partial images separately
from newly synthesized divisors. Functional search also reports single-divisor
and parallel-divisor realization attempts and peak deferred-table bytes; these
are attempted encodings, not final selected cell counts. The selection-cache
envelope is version 17 to retain decomposition, both boundary-ranking rounds,
pair gain-screen, bounded-window, joint-choice, fanout-free-window and logical-stage
work evidence, including joint functional-divisor generation, care completion and recoding,
plus the search's credit floor, the region's structural work and its identity
fallback count, and to reject records
predating this layout; standalone region artifacts keep
their existing version. Projected divisor tables and exact dependence
images share a separate per-search cache, capped at 64 entries and 8 MiB.
Its keys include the ordered independent basis and divisor set; exact dependence
conflicts are reusable, while exhausted attempts are not cached as failures.
When retention is refused, trials may recompute the image within their work cap.

Local search retains up to `local_divisors=32` existing signals and tries up to
`local_candidates=64` divisor sets (0 disables this tier). Already-paid
shared gates rank ahead of sources; fanout, level and stable node order break
ties. A set need not be a structural cut, but its exact image must determine
the endpoint function on the admitted basis. Cyclic/outside-cone dependencies
are excluded. Static and mixed first-phase realizations use the same evaluator
and accept only a strictly cheaper complete implementation. Shared readers
retain their logic and earn no deletion credit.
Failed dependence attempts retain two assignments with equal divisor values but
opposite endpoint values. Larger sets are built by adding a divisor that separates
that witness. At most 16 failed singleton seeds feed a depth-first search bounded
by the analysis input limit. Fixed-size seed/stack records retain selected IDs,
the current witness and excluded alternatives. Earlier non-separating divisors
remain available for later witnesses; sibling branches exclude earlier separating
choices. Divisor sets are canonicalized before image lookup.
For pools and budgets larger than two, singleton trials use at most the rounded-up
half of the candidate allowance. Each seed can spend half the remaining trials,
with the final seed permitted the remainder. Exact projections, witness recovery,
traversal and candidate evaluation all consume the local work budget. Witness
recovery uses a bounded rescan without an exponential assignment-index array;
an incomplete recovery is not cached as a dependence failure. Determining sets
use the existing care/cost evaluator; this heuristic does not enumerate every
set or prove optimal cost. Wider windows, gain-based poor-endpoint screening and
more general joint endpoint optimization remain pending.

Each endpoint search shares exact tables and gate results in one bounded cache,
keyed by root and ordered leaves on its immutable graph. A factoring refusal
retains the table without caching an infeasibility claim. The cache is released
after selection; residual feedback creates a new search on the changed graph.
Single-divisor functional attempts precede boundary enumeration, so a competitive
two-cell whole-cone result with no outside sharing skips that work. Wider
functional candidates wait until the existing-divisor two-cell pass finishes. Functional and
structural attempts share the two-phase work cap while retaining work for later
selective removal. Outside sharing is checked once under the admission budget.
`fast_accept=true` keeps this early-exit policy. Set it to false to continue
bounded boundary and local-divisor improvement after a full-cone result; all
existing tier and endpoint work limits still apply. A legal full-cone result
is not necessarily the cheapest implementation.
Boundary exploration retains at most `boundaries` candidates (default 32),
including the admitted window. It ranks exact legal single-endpoint plus residual
implementations and bounded full or mixed two-cell realizations with the same
sharing/inverter cost ledger as final selection. The latter price the first-phase
producer, endpoint and remaining static inputs together before pruning, preserving
improvements even if their cut is evicted. After the two-cell search and deferred
functional candidates, a second round ranks wider full and mixed structural
realizations. Competitive full-cone fast acceptance skips this extra round.
Both rounds also rank care-set completions before pruning. Correlated static
inputs can improve a one-phase endpoint; full or mixed first-phase producers
can improve a two-phase candidate. Every completion passes composition checking
and the same shared-cost evaluation before its cost affects the frontier.
Legal improvements survive eviction even when they use the final work credit.
When care search is enabled on a changed boundary, structural phase ranking
uses at most half its slice, reserving the remainder for full and mixed care
trials. These share the existing phase-ranking cap. Static-input care trials
use a bounded slice of remaining ranking work, charged only to boundary work.
The exact image cache is reused, and `care_phases=0` skips these new trials.
Alternating reconvergence and asymmetric-depth priorities retain intermediate
cuts even when their current cost is unattractive or factoring is inconclusive.
A root-outward seed is protected until expansion. New candidates can replace
previously retained cuts, and priced legal improvements are kept as incumbents
independently of frontier pruning. Single-leaf expansions expose asymmetric cuts;
paired and wider coordinated expansions can cross an intermediate support limit.
Growth may expose sources hidden by the original bounded window. For wider moves,
signals sharing a direct input form connected groups: try whole components, then
bounded connected prefixes from each seed. Contraction uses the same proposals
over at most the first 16 non-root interior nodes; growth uses the current leaves.
Fixed-size scratch storage and charged scans bound group discovery without
enumerating every subset. Both directions normalize reachable boundary signals
before checking support, so newly hidden leaves do not inflate the input count.
Truth tables are constructed only after the normalized cut fits the analysis cap.
Contraction and growth each admit at most `4*boundaries` move trials in the first
round and `2*boundaries` in the wider round, with one root-seed trial per round.
Thus one-phase search admits at most `8*boundaries+1` trials; two-phase search
admits at most `12*boundaries+2`. Duplicates and failed moves count. Contraction uses at most
half the remaining boundary-tier work. Ranking uses a bounded slice and shares
exact tables/gates with later two-phase and residual evaluation. Frontier payload,
seen-boundary keys and the active expansion are admitted against the remaining
boundary-memory allowance, including overlap before eviction. The best legal
endpoint survives work/storage refusal. Nested work slices propagate process
refusal without calling the guard again while unwinding.
Each ranking round uses at most one eighth of the shared phase-search allowance.
`boundary_two_cell_work` and `boundary_multi_cell_work` are subsets of
`boundary_work`; their sum with `two_phase_work` obeys the phase cap. Do not add
those subsets again when totaling stage work. The first round uses at most half
the boundary-tier cap when a second round is enabled. Deferred parallel
realization reserves half the remaining phase allowance for subsequent search.
The first frontier is released before admitting the second; exact analysis,
gate caches and the legal incumbent survive. A competitive full-cone winner
skips subsequent phase and residual tiers. One-phase mode has no second round
or joint phase ranking, and `boundaries=1` keeps the original admitted window.
Mixed structural and existing-divisor care candidates choose which legal
producers to convert by total shared cost. Starting from all-static and
all-available-producer seeds, bounded
greedy sweeps add or remove one producer at a time. Each direction makes at most
`n*(n+1)/2` trials for `n<=16`, plus its seed, under candidate/tier work caps.
The backward sweep can retain jointly profitable producers even when converting
either alone would leave their shared static logic live. It can also keep a cheap
AND in CMOS beside a profitable DOMINO producer. Producer bindings are rebuilt
for each subset; only priced legal improvements update the incumbent. These
trials use the current cell-count restriction, so the first ranking round admits
at most one producer, while the second round and residual search admit wider
parallel subsets. Repricing starts
from a fresh cost ledger and never accumulates the previous estimate.
Both seeds are evaluated before either greedy sweep can exhaust its budget.
Care candidates retain each existing producer's native identity during
construction. Demotion restores that identity in a private copy of the endpoint
function; retained producer slots remain zero and their indices are compacted.
The independent functional basis and completed truth table are preserved, and
each subset is checked by exact composition before costing or acceptance. This
also applies to mixed local-divisor candidates, preserving strict local gain
accounting. Fully covered attempts and newly synthesized divisors without a
native counterpart do not use this demotion path.
This is a bounded heuristic frontier, not complete priority-cut enumeration or
globally optimal boundary selection. Broader group discovery and benchmark-based
search tuning remain pending.
Reports expose search table builds/hits, retained cache bytes
and boundary work/trials/replacements/wins/admitted peak bytes, divisor-image builds/hits/bytes, and local attempts/wins/work;
table counters exclude independent composition validation.

`residual` optimizes a shared multi-output XAG with cleanup, one four-input
rewrite sweep using a small native template library, bounded truth-table
resubstitution, and cleanup. Defaults are eight window inputs, 32 divisors
and at most two inserted nodes. The library is heuristic, not complete optimal
four-input synthesis. Incremental reference counts protect outside readers,
including consumers not rebuilt yet; strict weighted cost improvements and
equal-cost replacements with strictly lower depth pass, subject to a depth
guard. Work/node limits retain the last complete
network. Changed-output flags identify inputs for endpoint feedback.

`//pass/usyn:usyn_lnet` uses the small `//pass/synth:lnet` library rather than
the shared driver's formal/LEC dependencies. It has no ABC dependency, imports
and exports Lnet logic, and preserves input/output and register Q/D/name/init
correspondence. `endpoint_lnet` expands selected formulas into a single
small-fanin CMOS network, retaining all original registers and adding no state
for first-phase cells. It checks endpoint correspondence before emitting a
result and prunes dead logic.

`logical_region` connects endpoint selection, coordinated pair reselection,
shared residual optimization and one affected-endpoint feedback round. The caller supplies eligible state indices;
the pipeline preserves other state as explicit boundaries. It retains separate
cell formulas and native-input bindings alongside the complete CMOS expansion,
rebases those bindings after residual rewrites, and rechecks total mixed-network
cost including shared static inversions. Logic absorbed by one endpoint still
belongs to the residual when another reader needs it. Changed shared boundaries
mark all dependent endpoints for the single feedback round; final cleanup does
not launch another rewrite sweep. A resource refusal retains a complete selected
incumbent. Before any search, the structural ledger builds and prices the
minimal legal selection -- every eligible endpoint as its identity endpoint, one
DominoLatch fed by its whole static D cone -- so a search that cannot finish an
endpoint keeps that endpoint's identity. A search that stops while pricing its
selection keeps its searched endpoints: a selection of identities is this
already priced baseline, and one with a searched cell is priced on the
structural ledger instead. Only a selection beyond the node limits publishes
the baseline, rebuilt from the source with the copy charged to the structural
ledger. `identity_fallbacks` counts the fallback identities the region still
publishes (a later pair or feedback win can replace one), so each counted
fallback is an endpoint whose origin is `identity`; a cache record checks this.
Whole-cone admission, initial selection and feedback remain bounded;
global reference-ledger reuse across endpoint searches is still pending.

`endpoint_pairs` propagates live residual endpoint labels backward through the
XAG, retaining two distinct labels and saturating at more than two. Shared gates
reaching exactly two selected endpoints in a known common clock domain seed
bounded pair trials. The semantic adapter identifies domains from the actual
clock event signal and edge, so distinct gated clocks do not merge merely because
they share a root clock. Empty/unknown domains disable pair trials for graph-only
callers; no domain is inferred from register names.

Pair search reselects both functions on the same immutable graph,
protecting every other consumer, then validates and scores the complete mixed
network. Both selections commit together only on strict total-cost improvement;
refusal retains the best complete pair. This can delete a shared residual whose cost
neither endpoint can recover alone. Each endpoint also retains its existing
legal implementation as an alternative. Evaluate at most three combinations
on the same snapshot: replace both, replace only the first, or replace only the
second. Price each complete shared network and atomically publish the best
strict improvement; stable ties retain the first candidate. A later copy,
expansion or cost refusal preserves an already priced winner. No intermediate
combination changes the graph used by the other trials. `combinations` counts
admitted candidate snapshots and is at most three times pair `attempts`.
This bounded comparison can retain a cheaper existing partner when two
independently selected replacements lose shared-logic reuse.

After this original greedy trajectory finishes, `pair_choices=4` enables a
second bounded sweep. Each endpoint search retains up to four differently
shared static interfaces, including candidates that lose its individual cost
comparison. Input identities and demanded inverse rails define an interface;
for an identical interface, only the least DOMINO cost survives. Different
interfaces compete for bounded slots by individual estimated cost, so this is a
heuristic pool, not an exhaustive Pareto set. Retention charges candidate work
and has a separate `boundary_bytes`-sized payload cap per endpoint, including
conservative function/table/formula storage even when functions share ownership.
The endpoint's selected winner survives independently of this pool.

The second sweep disables early acceptance within its endpoint searches and
uses the existing structural, functional and local-divisor generators. Validate
retained choices on the admitted basis, then price their combinations with each
other and the original implementations. The first three best/old combinations
remain first; `choice_combinations` counts additional fully admitted snapshots,
and `choices` counts validated pool entries beyond the selected winners.
With a limit N, at most `(N+2)^2-4` additional combinations are attempted per
admitted pair. All pair sweeps share `pair_trials` and `pair_work`; zero disables
the additional choice/window sweeps, and the maximum pool setting is eight. The second sweep cannot
replace the first sweep's complete result with a more expensive network.
This ordering matters: an immediately cheaper combination can otherwise alter
structural cuts and hide a later improvement. Cancellation preserves the best
fully priced network. Bounded joint functional-divisor generation follows these
sweeps as described below. Identical selected
phase-one implementations now share across endpoints as described below.

Before reselection, a conservative gain
screen prices the unchanged outside consumers, including their shared gates and
required inverters, then adds two mandatory latches and a lower bound on their
logic. Sixty-four bit-parallel source assignments can witness nonconstancy,
which requires at least one transistor per affected endpoint. Uniform samples
are inconclusive and contribute a zero-transistor lower bound. The screen skips
only when this optimistic cost cannot beat the incumbent; `gain_skips` counts
these trials separately from attempted reselections and admission failures.
This bound applies to reselection with unchanged outside implementations, not
future rewrites of those outside outputs. Sampling and accounting consume the
existing pair work budget. Process refusal preserves the incumbent and unwinds
work slices without calling the admission guard again.
Defaults retain 32 pairs, admit a joint basis
of at most 16 independent inputs, and spend at most 16 million work units (also
capped at one quarter of the remaining regional budget). Each endpoint has its
own support cap as well. Combined cone-node admission conservatively counts a
shared node in both cones. When full cones do not fit, a common structural
boundary grows from the two roots under the same joint/per-endpoint caps.
Each step prefers fewer exposed signals, then stable reverse-topological order;
unused leaves are removed. Growth reserves half the remaining pair work, capped
by endpoint candidate work, and retains its last complete window on work refusal.
Both endpoint searches are pinned to this admitted boundary: contractions and
factoring stay inside it, while its upstream implementation remains static.
Correlated internal leaves are conservatively treated as independent formal
inputs; the gain screen does not infer source-function nonconstancy from them.
Outside readers still use the full original graph for cost and deletion credit.
`bounded_windows` counts admitted fallback windows; support/window skips also
record failed full-cone admission even when bounded growth subsequently succeeds.
This greedy window is not a maximum-fanout-free-window optimizer.

After the original greedy and interface-choice trajectories, remaining pair
work can try a maximal private joint cone relative to the unchanged outside
implementations. Trace the actual live outside roots: primary outputs,
unselected state inputs and other selected endpoints' static dependencies.
Stop each pair cone at sources or nodes in those outside cones. The resulting
explicit outside ports stay in the common basis, with their implementations
and demanded polarities protected by the full regional ledger. Internal logic
shared only by the pair remains inside the window. Historical `Node::fanouts`
do not participate: dead alternatives in the append-only graph must not block
absorption. A directly observed pair root becomes an identity window.

The traversal has graph-storage, joint/per-endpoint support, combined-node and
work caps. Refusal publishes no partial window; windows with no internal outside
ports skip redundant whole-cone trials. Admitted searches stay pinned to their
basis and use the same validated choice pools and atomic strict-cost acceptance.
This final sweep cannot replace either preceding trajectory with a worse result.
`fanout_windows`, `fanout_ports`, `fanout_skips` and `fanout_wins` distinguish its
admission and gains. It optimizes private endpoint implementations while keeping
outside functions fixed; rewrites that change those protected shared implementations
remain pending.

With two phases and nonzero `pair_choices`/`divisor_partitions`, an additional bounded
sweep synthesizes common first-phase code functions for a pair. Discovery uses
the complete selected next-state functions, including absorbed logic, because a
shared cell can beat two already legal full-cone implementations. In this tier
only, two distinct sources reaching exactly the same two endpoints can also seed
a pair when factoring left no common internal node. Pending source pairs and
accepted candidates are capped independently by `pair_candidates`; one common
source, unknown/different clock domains and saturated three-endpoint sources do
not establish a candidate.

Reuse a common exact basis and enumerate up to `divisor_partitions` bound sets
per admitted pair. Bound inputs must occur in both endpoint cuts; free inputs
may be private to either. Classify each bound assignment by both outputs' full
cofactors over the free variables. Binary class codes define shared divisors;
each output receives its own top function. Factor each producer once, compact
each top's support, discard producers unused by that top, and independently
validate both endpoints on their own original cuts. At least one producer must
be used by both endpoints. The joint basis may exceed an individual cut's cap;
neither individual cut may exceed it.

The gain screen includes a minimum new nonconstant first-phase cell unless an
unchanged same-domain producer could supply it. Price both replacements and
each replacement with its original partner on one immutable snapshot. Publish
only the best strict regional gain, retaining outside static costs and rail
demands and charging identical first-phase cells once. Failed later partitions
or cancellation preserve a fully priced incumbent. The stage shares the existing
pair trial/work caps, uses `candidate_work` per encoding and bounds transient
class/formula storage with `boundary_bytes`. It adds no solver or ABC calls.

Reports/cache retain `joint_source_pairs`, `joint_windows`, `joint_partitions`,
`joint_candidates`, `joint_divisors`, `joint_combinations` and `joint_wins`.
The initial generator uses deterministic binary class codes and zero completion
on unreachable code words. A final care-completion sweep follows this complete
trajectory when `care_phases` is nonzero. It retains a legal zero-filled top and
up to `pair_choices` alternatives per endpoint, trying one fill and bounded
least/greatest signed-monotone completions. Full code images skip this search.
Each top is independently validated and compacted; unused producers are removed
before checking their availability. A top can drop every proposed producer, so
the care sweep omits the mandatory-new-producer gain bound. It prices retained
cross-combinations and each changed top with the original partner, committing
only strict regional improvement. At least one nonzero-fill alternative must
participate; zero/old-only combinations belong to the preceding trajectory.
Validated entries survive later refusal, and a fully priced incumbent survives
cancellation. Work, choices and transient storage use the existing pair,
candidate and boundary caps. Completion scratch and retained payload estimates
are charged together while live; their peak estimate is not process RSS.
Reports/cache add `joint_care_windows`, `joint_care_partitions`,
`joint_care_phases`, `joint_care_attempts`, `joint_care_retained`,
`joint_care_bytes`, `joint_care_combinations` and `joint_care_wins`.
A final bounded recoding sweep tries invertible `code[target] ^= code[source]`
changes, up to `pair_choices` per partition, with consistent permutation of both
tops and the care image. It independently validates and prices the same joint
and original-partner combinations; full images and `care_phases=0` still allow
recoded zero completions. Reports/cache retain `joint_recode_*` evidence. This
is not exhaustive encoding optimization. Mixed static/DOMINO implementations
of code bits and a general joint node/edge solution forest remain pending.
Each commit refreshes live sharing before another
trial. Paid static gates and demanded inverse rails from the old/new input cones
identify conservatively affected endpoints; a common positive source alone does
not invalidate unrelated trials. Previously failed unchanged pairs are skipped,
affected pairs can be retried, and pending work survives unrelated commits.
Pairs absent from the refreshed sharing snapshot are discarded. Discovery is
currently recomputed under the same regional work cap, rather than maintained
incrementally. `pair_trials=64` caps all popped candidates, including revisits
and support-admission failures; reaching it retains the last complete result.
Reports separate trials, admitted attempts, refreshes, requeues, stale skips,
work, gains and the cost after pairs from residual optimization. Discovery counts
are cumulative across snapshots, not unique physical pairs or gates.
This is a foundation for G: larger maximum-fanout-free windows, joint node/edge
divisor solution forests beyond these retained choices, sharing of differently
represented equivalent cells and incremental discovery remain pending.

The shared blaster now supplies owned source-state metadata to region hooks,
including native barriers, source spans, structural clock-gate identities and
semantic Q/D references that undo QN encoding. Its target validator can inspect
reachable source definitions before partitioning.

`//pass/usyn:usyn_semantic` connects that snapshot to the logical pipeline.
`semantic_region` validates the requested target, restores semantic D, derives
eligible endpoints from source roles/clock edges, and carries owned controls,
locations and bit/stage correspondence through selection and CMOS expansion.
Ordinary registers missing from the logical boundary cause a refusal rather
than silently disappearing from endpoint selection. Special native barriers
remain in the snapshot. State names use source spelling, with source-identity
suffixes for collisions; source names remain available unchanged in provenance.
The snapshot's Lid values identify the original RAW Lnet only; current Q/D
signals come from the selected logical state's correspondence. The blaster's
`logical_state` mode translates ordinary registers independently of Liberty,
`map_register`, and QN mapping choices. It keeps dynamic clocks, asynchronous
resets, and initialization values as protected output indices that survive
optimization; memory, ICG, and opaque structures remain native boundaries.

`endpoint_netlist` freezes the final selection into an ABC-independent in-memory
graph with Static residual logic, Domino cells and DominoLatch storage owners.
Each cell carries its exact function, formula, ordered inputs, explicit rail
demands, Q/!Q ports, phase and source-state correspondence. State indices retain
the semantic wrapper's complete clock/reset/init/source metadata. Validation
checks formula truth tables, recomputed legality and shared cost, storage
ownership, topological phase dependencies and clock domains. Static edges cannot
refer to same-cycle cells, preventing hidden inter-phase CMOS. The representation
shares identical phase-one implementations across endpoints in the same known
clock domain. Regional selection, pair and feedback costs charge these cells
once; freezing interns the same identity and rebinds all consumers. The identity
contains the ordered native inputs and exact formula representation, phase and
clock domain. Opposite formula output polarities reuse the same physical cell
through Q/!Q aliases. Unknown domains only permit reuse within one state owner;
different known domains remain separate. Every endpoint retains its own
DominoLatch and source-state name. Static outside consumers keep their native
logic and any required inverter; they cannot consume a same-cycle DOMINO rail.
This does not discover general Boolean equivalence between differently factored
cells or jointly invent a shared divisor. CMOS reconstruction must reproduce
each recorded canonical next-state expression and retains exactly the original
registers. Invalid or exhausted validation returns no partial graph.

Report `selected_cells` continues to count per-endpoint cell uses. The new
`frozen_cells` counts unique selected physical-model cells, and
`shared_phase_one_uses` counts duplicate phase-one uses eliminated by sharing.
The latter two fields also appear per region. These are definition-region
DOMINO model counts, not the CMOS cells produced by optional technology mapping.

Every successful stateful synthesis result owns this frozen netlist.
`logical_writer` validates and expands it into an owned bit-level
graph module, with one native register per original bit/stage and no phase-1
storage. It preserves source names/locations, clock polarity, full-width async
reset controls, dynamic reset/initial values and unknown constant initial bits.
Synchronous reset and enable stay in D. Input/output port order explicitly
matches the logical boundary, including protected controls; refusal publishes no
partial graph. This writer needs neither a Liberty library nor ABC.

`region_emit` reconnects that module to packed region ports and native memory,
ICG, opaque, and compact-loop boundaries without adding a wrapper hierarchy.
It carries native attributes and source locations, resolves already-emitted
concrete children, and retains wide native buses without bit expansion. Sparse
bit extraction uses a shift and one-bit mask rather than a large one-hot mask.

`design_synth` connects private-copy preparation, children-first partitioning,
logical selection and native emission for a complete CMOS design. It preserves
hierarchy and compact loops, owns the resulting library and per-region selection
records, and discards all output on refusal. This callable path requires neither
ABC nor Liberty and invokes no SATOPT. Integration tests cover hierarchy,
colored boundaries, opaque definitions, negative-edge state, and refusal/retry;
the emitted next-state function and clock are checked exhaustively on small inputs.
Private preparation now uses the invocation's work/process admission: it counts
source nodes once per copied definition, charges declared ports and primary/body
pins/edges before each copy, and samples around bulk copy and loop preparation.
Refusal discards the private library and reports search exhaustion, including
refusal at preparation completion. This is structural work: it is charged to
the structural allowance, never to region search credits (see "Work accounting"
below). A new budget can retry against unchanged sources. Partitioning shares the same admission during definition ordering,
region collection, boundary naming and reconstruction. Zero-work checks bracket
builders and commits; a failed builder stops further region construction.
Even refusal at final partition completion discards all output and reports
search exhaustion. Flattening charges each instance's context, node/pin/edge
walks and cross-module driver resolution; a new opaque interface is admitted
once across repeated instances. Forward and producer boundary signatures charge
their reachable-set, ordering, edge and signature-fold walks. Their cancellation
stops before another callback or builder. Individual HHDS/helper allocations,
sorts and value/string serialization remain bulk operations; these checkpoints
are not a strict peak-byte bound.

The optional `pass/synth/tmap.hpp` capability accepts the resulting top and
Liberty/settings, returning a separate owned mapped design. Its ABC provider
lives entirely under `pass/abc` and uses only the mapping handoff, with no USYN
link dependency on that package. Integration tests map a native USYN artifact,
evaluate the mapped next-state function/clock/output over all small assignments,
and check that command-boundary refusal publishes no result. The logical design
and its selected formulas remain authoritative after mapping.

The public `//pass/usyn:pass_usyn` dependency closure excludes ABC, including
its generated salt and public-entry test. The full `lhd` executable can also
exclude ABC with `--define=livehd_abc=false`; see the
[build and removal gate](../../lhd/README.md#abc-free-build).
`target=cmos` is currently supported;
physical DominoLatch output and public DOMINO-target validation remain pending.

## Public options and reports

All options below use the `pass.usyn.` prefix.

| Options | Defaults and purpose |
|---|---|
| `tmap`, `target` | `abc`, `cmos`; `tmap=none` emits logical CMOS |
| `adder` | `auto`; prefix carry trees for Sum cells at least 16 bits wide, ripple elsewhere. Explicit `rca`, `cska`, `cla`, `prefix` apply throughout arithmetic |
| `adder_block` | 0; derive CSKA/CLA group width from operating width |
| `logical_inputs`, `stack`, `branches` | 8, 4, 10; gate legality |
| `cut_inputs`, `window_nodes`, `boundaries` | 16, 100000, 32; analysis limits and retained frontier (at most 12×boundaries+2 move trials; one phase: 8×boundaries+1) |
| `divisor_partitions` | 32; new functional-divisor partitions, 0 disables, maximum 4096 |
| `care_phases` | 16; input-polarity trials for top care completions, 0 disables, maximum 64 |
| `local_divisors`, `local_candidates` | 32, 64; retained local signals (1..256) and witness-guided set trials (0 disables, maximum 4096) |
| `fast_accept` | true; false continues bounded improvement after a competitive full-cone result |
| `clock_phases` | 2; accepts 1 or 2, with parallel cells; adds no CMOS state |
| `work`, `endpoint_work`, `max_nodes` | 4000000000, 16000000, 2000000; deterministic admission (`work` is both the structural allowance, which also pays every region's mandatory import/identity-selection/freeze steps, and the search remainder that region searches share in order, see "Work accounting") |
| `pair_candidates`, `pair_inputs`, `pair_work` | 32, 16, 16000000; pair retention (0 disables, maximum 4096), joint input cap (1..16), regional pair work |
| `pair_choices` | 4; distinct static-interface candidates retained per paired endpoint after the initial pair sweep (0 disables, maximum 8) |
| `pair_trials` | 64; total pair candidates visited, including affected retries and admission failures (1..4096) |
| `static_and`, `static_xor`, `static_not` | 2, 4, 1; positive provisional costs |
| `residual`, `feedback` | true, true; one residual round and affected-endpoint feedback |
| `residual_inputs`, `residual_divisors`, `residual_inserted` | 8, 32, 2; residual search bounds |
| `residual_depth_slack` | 0; no residual depth growth |
| `flatten` | auto; follows coloring, or explicitly true/false |
| `delay` | 0; optional tmap timing target in ps |
| `memory_budget_mb`, `time_budget_ms` | 16384, 0; memory-growth and elapsed-time admission |

Old cover options such as `support`, `literals`, `series`, `domino_levels`,
`abc`, and `fallback` produce migration diagnostics. They are not aliases for
the new cost or phase policies. `synth.liberty` supplies the mapping library.

The automatic arithmetic policy constructs wide carries in logarithmic depth
before native selection. It keeps multiplier/divider internals and narrow sums
compact. Prefix trees can increase area; `adder=rca` restores the previous
lowering for an area comparison. The policy changes the imported Lnet, so its
logical cache keys also change. It does not invoke ABC Boolean optimization.

`<qor>.usyn.json` is the schema-5 endpoint/residual decision report. It includes
selected cell formulas and bindings, search limits, new-divisor attempt counts, cost estimates, preserved
register counts, and input provenance. Each region also reports its
`search_credits`, its search's `credit_floor` (`work`, `floor`, `bound` and,
when bound, the exact `credits`; `search` is the separate per-endpoint search
array), its `structural_work` (import, logical admission and identity baseline,
any structural pricing or rebuild, freezing) and `identity_fallbacks` (fallback
identities still published); `cache` adds `credit_misses`, `kept` and
`replayed_structural_work`. These are additions to schema 5. Its counts describe definition regions,
not physical instance totals. `qor.json` contains either this logical report or
the separate `technology-map` report for mapped output. Fused `lhd synth`
embeds selection under `qor.usyn`, optional mapping under `qor.abc`, and optional
STA under `qor.sta`. Logical-only output has `qor.abc=null` and no STA report.

Each report region now links a version-1 binary artifact under
`<qor>.usyn.artifacts/<sha256>.usyn`. `artifact` serializes the complete frozen
netlist and owned source/control wrapper, including wide unknown constants,
source spans, clock-gate metadata and bit/stage correspondence. Loading checks
the checksum, format, field bounds, functions, phases, costs, source eligibility
and clock domains. The current requested target is validated even when loading
a CMOS artifact. Encoding and decoding have separate byte, object, node,
string/formula and decoded-storage admission limits; refusal publishes no partial
object. The writer can expand a loaded artifact without ABC or rerunning search.
Content-derived filenames and per-file atomic replacement preserve existing
artifacts during a failed later invocation; a successful publication removes
the artifacts only earlier reports referenced. `--emit-dir report:` copies the
artifact directory with its report. These are region artifacts; reconnecting
native barriers and hierarchy still belongs to the design owner.

### Work accounting

`work` sets two deterministic ledgers of that size, both sharing the
invocation's process/time admission:

- The structural allowance pays for private preparation, partition and region
  admission, logical translation, native emission and every region's
  mandatory synthesis steps: semantic import, the logical region's admission
  and boundary validation, the minimal legal selection (each eligible endpoint
  as its identity endpoint) with its validation, expansion and pricing, the
  pricing of a selection with a searched cell when the search ran out of
  credits pricing it (or the baseline's copy when a selection exceeds the node
  limits), and freezing the published selection. These steps use plain spends
  only, so their charge is a deterministic function of the region and its
  search credits; it never enters a search's credit floor. Exhausting the
  allowance refuses the design (preparation, partitioning, a region's
  mandatory steps or its emission, whichever runs out) and publishes nothing,
  so a design whose structural work fits `work` never fails for lack of it.
- Region searches (endpoint selection and its expansion and pricing, pairs,
  residual and feedback) share a search-only remainder. Regions run children
  first; region *i* starts with `work` minus the search work of regions before
  it. Preparation, partitioning, translation, emission and the mandatory steps
  of any region therefore never change a search. A heavy region can leave
  little for later ones: those degrade instead of failing the design. An
  endpoint whose search cannot finish keeps its identity endpoint; a search
  that runs out while pricing its selection keeps its searched endpoints,
  priced structurally when any is a searched cell (a region with no search
  credits at all publishes exactly the identity baseline); later stages keep
  their complete incumbent. The sum of the searches never exceeds `work`.

This split moved work out of the searches, intentionally: import, the
logical admission/validation (`report.work.admission`, which now also includes
the identity baseline and any structural pricing or rebuild after the search)
and freezing no longer draw on search credits, and a
region with no eligible endpoint prices its selection only once, structurally
(its `selection` stage is now 0). An unbound search keeps every other stage's
work exactly; a bound one, which ran out of credits, sees the credits those
steps used to take and can decide differently.

Any process/time refusal is sticky, fails the design and publishes nothing.

`Budget` (unate.hpp) makes a search's dependence on its credits explicit. It
tracks the work `consumed`, a `floor` -- the least initial credits under which
the same run happens -- and `bound`, meaning the run depended exactly on its
initial credits. A successful spend needs initial credits of at least the work
consumed after it; a spend refused for lack of work, or a failed `has()` check,
binds the run. Every decision that reads the credits goes through such a call:
`has()`/`available()` for remaining-work checks, `Credit_share` for a shared
sub-cap that is compared later (only the answers it gives are recorded), and
`slice(cap, divisor, reserve)` for a child of `min(cap, (remaining - reserve) /
divisor)` credits. `absorb()` lifts the child's requirement: an unbound child
needs its size to stay at least its floor (`remaining >= reserve +
divisor*floor` at the slice); a bound child needs its exact size, which a
cap-limited size keeps for `remaining >= reserve + divisor*cap`, while a
remaining-limited size binds the parent. No search code reads `remaining`
directly. Process/time admission refusals are not reproduction facts; such
results are never reused. For a search that is not bound, any credits at or
above its floor replay the identical search: the same decisions, evidence and
consumed work (the unit, endpoint and region tests check this on generated
networks). A search that exhausts its credits is usually bound, and heavy
regions usually exhaust their share: region searches size their stages from
what remains, so a heavy region spends most of any credits it gets (an FTQ
region is bound even at the default 4G). A bound record is reused only under
exactly its credits. An edit that changes an earlier heavy region's search
work shifts the credits of the regions searched after it; an unchanged region
is then reused (as long as its stored record was not replaced, see below)
exactly when its credits did not change, or when its search was unbound and
its new credits still reach the recorded floor. Unbound does
not mean complete (`search_exhausted` may still be true, from per-endpoint
caps), and small regions can be bound too: a bound region whose credits
changed is searched again.

### Logical region cache

Persistent logical region reuse follows the single `lhd.incremental` switch and
requires a named work directory. The cache key (identity version 3) includes the
complete RAW logical network, source/control semantics, register/latch/clock-gate
names, the memory/instance names of native barriers, the top-level design IO
names a region binds, native producer salt, search/cost policies and resource
settings. Search credits are not part of the key. ABC, Liberty and tmap settings
do not enter it either. Nor do intermediate names, which equivalence checking does not
match and which usually disappear: definition/module and source-graph names,
source spans, local node numbers (only their equalities are kept) and every
other region-boundary net spelling. A definition's own ports are such nets; only
regions of the top definition bind design IO (the partitioner names the scratch
definition of a whole-design flatten through `partition::flatten_scratch_name`).
Each entry (selection record version 17) owns its frozen netlist and decision
evidence plus the search's credit floor: its consumed work, floor, whether it
was bound and, when bound, its exact credits; and the region's structural
work. Loading checks integrity, semantic consistency, that floor record, that
the structural work covers the logical admission stage and that no more
identity fallbacks are counted than endpoints of origin `identity` are
published. A record is a hit
only when the region's current search credits reproduce it: at least its floor
for an unbound search, exactly its credits for a bound one. Otherwise the
lookup is a credit miss and the region is searched again. Its result replaces
the record unless the missed record is unbound and the re-search bound (below
an unbound floor it always is): the more general record is kept (counted in
`cache.kept`), so the original credits hit again while the lower credits keep
missing. Bound records under one key replace each other, latest wins, so two
same-key regions bound at different credits keep missing each other's record.
A hit replays its recorded search work on the search remainder and its
recorded structural work on the structural allowance, so later regions receive
the credits a cold run gives them and the structural total equals a cold
run's (its single replayed spend refuses exactly when the cold steps would).
An unchanged region therefore reuses its decisions when an earlier region is
edited, grown or inserted, as long as its new credits still reach its floor;
only a bound record (typically a region that exhausted its share) needs the
identical credits.

A hit is then rebound to the fresh translation: its module name, RAW port spellings
(ports bind by position) and complete source snapshot, including spans. The
rebind refuses a snapshot that differs in an identity field or whose fresh
import yields other state names or clock domains (for example a boundary net
now spelled like a register). A rebound hit therefore publishes the same
artifact as a cold run. Renaming or respecializing a definition (`lane` to
`lane_p1`) or an internal net reuses its decisions; renaming a register, memory,
clock gate or top-level IO regenerates the regions that bind it. Fresh region
emission reconnects the current hierarchy and native barriers. Invalid entries
regenerate.
Writes use atomic replacement; an interrupted store can leave a `<key>.tmp-*`
file which, like superseded entries, is not collected automatically. Disabled
caching performs neither reads nor writes.

Cache I/O has a separate bounded work allowance and shares invocation memory/time
admission. Hits never repeat a search, but the structural allowance, the search
remainder and every region's search credits of a warm run equal a cold run's.
Thus report search counts describe the retained decision evidence;
`cache.io_work` measures cache work, while `cache.replayed_search_work` and
`cache.replayed_structural_work` identify replayed credits, not repeated work.
Reports expose enabled/reused/miss/invalid/refused/credit_misses/kept/store
counters and region keys; `credit_misses` is a subset of misses and `kept` a
subset of credit misses.

### Mapping cache

The optional ABC provider keeps a separate mapped-region cache under
`usyn_cache/tmap`, controlled by the same switch. It uses the mapped input's
structural identity, provider salt, Liberty content, resolved state-cell choices
and mapping recipe. Library or timing changes remap without invalidating native
selection. Complete snapshots include the mapped and pre-mapping graph bodies;
bounded integrity checks precede graph loading. A new immutable generation is
published through an atomic `current` pointer only after mapping succeeds.
Prior generations remain in the user workdir so concurrent readers stay valid;
automatic generation garbage collection is not implemented. Cache I/O failure
permits fresh mapping, while process/time refusal aborts publication.
The technology-map report's `incremental` object gives enablement, hits, misses,
invalid snapshots and failed stores, separately from native decision reuse.

Broader legacy-test migration, advanced endpoint search, and resource admission
inside individual bulk allocations and helper operations remain pending.

The old `lut_cover`, `usyn_region`, and evidence helpers remain for migration
and existing focused tests. They are no longer the public pass algorithm.

## Guarded comparisons

Build `//lhd` and `//pass/usyn:measure_synth` in the same mode, then run:

```sh
python3 pass/usyn/compare.py design.v --top top --liberty cells.lib \
  --workdir comparison-new --residual-ablation \
  --usyn-set logical_inputs=8 --usyn-set clock_phases=2
```

The driver compiles once and gives every variant a private copy of the same
USYN-profile colored snapshot. Fresh workdirs make the comparisons cold. Commands
run serially through `measure_synth` with common wall/process-tree memory limits;
`--delay` applies the same mapping target to both mappers. Native technology-map
reports include each region's applied `budget` in ps. The separate fused-command
smoke also checks explicit USYN delay settings independently of ABC settings.

Without `--residual-ablation`, compare one configured USYN run with ABC. With it,
compare selection alone, selection plus residual optimization, and selection plus
residual optimization and feedback, followed by ABC. Endpoint settings and work
caps stay fixed across the USYN variants; conflicting residual/feedback overrides
are rejected. `--compile-set` passes explicit shared compile settings through,
including a requested `pass.satopt=true`; the driver does not enable SATOPT.

`comparison.json` schema 2 retains source/Liberty digests, effective overrides,
native definition-region cost stages and coverage, endpoint/pair work counters,
mapped STA metrics and their completeness flags, and guarded invocation
time/peak-memory observations. Each native region's `work` object partitions all
budget charges within logical synthesis into admission (structural: admission,
validation, the identity baseline and any structural pricing or rebuild of the
initial selection), initial selection
(including initial expansion/pricing), pairs, residual optimization (including
rebinding/pricing), endpoint feedback, and final cleanup. `total` is their sum,
including refused and rolled-back trials. The comparison's `logical_work` sums
these counters over definition regions. Endpoint search counters and pair work
are nested detail, not additional charges. Cached regions replay the original
search evidence; these are deterministic budget units, not current-run time.
Translation, freezing, cache I/O and tmap lie outside this logical-region scope.
Rewrite versus resubstitution work and per-stage timing still need finer
instrumentation. The mapping invocation measurement includes native selection
and tmap for USYN. Missing memory readings
remain null. Raw reports, command arguments, logs, netlists and measurements stay
in the workdir. Estimated DOMINO cost is separate from mapped cell area.

Independent `lhd lec` checks use the generated Liberty models and are retained in
the comparison report; they do not enter USYN synthesis or cache acceptance.
`comparison_smoke` covers output-only logic and fused option forwarding;
`comparison_seq_smoke` covers original state, stable endpoint names and all three
residual configurations. Large corpus experiments remain in benchmark repositories.

### Performance and QoR checkpoint

Support/polarity analysis, formula verification, care fills and monotone
completion use packed 64-bit operations. Formula simulation reuses one scratch
vector per table, avoiding allocation per assignment. Admission charges words
and formula nodes; divisor-image construction and cover search still have scalar
loops. No Kitty or mockturtle dependency has been introduced.

Endpoint pricing shares a read-only protected-cone ledger and visits only the
candidate's added logic and inverse-rail demands. It no longer copies all protected
sets per candidate. The XAG is still append-only: mutable substitution and live
MFFC accounting are pending. Search and independent validation use separate
bounded allowances, both charged to the regional total, so heuristic exhaustion
does not invalidate a retained candidate. Process cancellation remains sticky.

`compare.py --source-extra PATH` accepts additional shared compilation units;
`--sdc PATH` sends identical constraints to each timer. Source/constraint digests
are retained. Failed mapping, timing or LEC variants retain their failure stage,
result and measurements, and the remaining variants continue. The comparison
still exits unsuccessfully if any variant fails or equivalence is unproven.

Additional search tiers are deferred while mapped measurements guide hot-path
work. Cheap preselection rewriting, a complete rewrite4 database and a priority-cut
DOMINO mapper are experiments to compare, not accepted dependencies or completed
features. The present timer can report incomplete clock/constraint support;
those delay values must not be presented as complete sequential timing results.

The first [ten-design mapped baseline](../../repros/usyn_qor_20260927/README.md)
retains all 40 mappings, 34 proven checks and six LEC timeouts. It exposes CMOS
area gaps and cases where feedback improves the native estimate but worsens
mapped area. The report also contains separate before/after kernel measurements;
no whole-flow speedup is claimed.
