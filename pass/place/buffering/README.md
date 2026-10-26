# Buffer candidate evaluation

`candidate` is the shared local evaluator for expanded buffering alternatives.
It does not edit a graph or materialize cells. `tree_search` now produces and
selects expanded alternatives at explicitly supplied tree sites. `negotiation` allocates finite alternatives against a shared site/resource ledger.
Scalable van Ginneken/CPB/BEN selection, production checkpoint integration and
the virtual buffer pool remain separate work.

Each candidate gives every inserted cell's model, physical input/output
positions, body and resource demand. One explicit RC tree drives each stage;
trees contain wire capacitance only. Library rise/fall input capacitances are
added once. Every original sink and inserted input must occur exactly once, and
the stage graph must be a connected tree. Pin identity, physical positions,
finite rectilinear length bounds, body overlap and declared routing exclusions
are checked before timing.

The evaluator walks stages forward with the existing native GaTe/pi driver and
per-receiver waveform kernels. Actual incoming slew selects NLDM values at every
buffer. Fresh loads include all affected branches, including noncritical sinks
after isolation. Input/output slew and output-capacitance limits remain hard
constraints. It then walks actual wire/cell delays backward to compute absolute
required time at every stage and the existing driver's physical input.
Unconstrained states stay missing. Required-time satisfaction is reported
separately from electrical/resource feasibility: an infeasible requested period
does not become a feasible one through objective weighting.

Dimensionless endpoint multipliers price delay divided by the supplied period
normalization; they never act as time offsets. A selected unconditional source
arc can be positive or negative unate. Inserted cells must be supported
noninverting one-input buffers. Full graph timing must verify source-arc context
and rank changes before any edit commits. These local results do not certify
whole-design timing, OpenSTA fidelity or routed feasibility.

Resource counts use exact integers, include all inserted cells and reject
oversubscription, including zero-slot resources. Capacity must be supplied
explicitly. This does not build row sites or negotiate competing nets. Work
bounds, unsupported/numerically failed models and evaluated-but-infeasible
candidates have distinct outcomes; none implies a search optimum.

Fourteen hermetic checks include all 120 assignments of fanout 1–4 to direct
connections or two buffer types, compared with independent NLDM formulas.
Other cases cover cascaded input-slew dependence, backward requirements,
negative-unate sources, critical isolation, electrical crossings, native lumped
wire threshold conversion, real distributed-fit failure, unit scaling,
capacity/body/routing constraints and malformed cyclic geometry.

`sites` builds compressed legal row-site intervals in integer database units.
Rows must have positive aligned pitch and disjoint bodies. Explicit exclusion
rectangles receive the requested halo plus legalization margin; overlapping
halos and nonrectangular unions block each intersecting site once. Touching
edges remain legal. The caller supplies the actual occupancy masks, including
current movable cells when computing sites for physical materialization. This
module does not infer routing capacity or convert a floorplan automatically.

`buffer_slots` counts nonoverlapping equal-width footprints, rather than all
possible starting positions. `Site_reservations` assigns every occupied site
to one owner across competing nets. Replacement validates the entire proposal
before committing; unavailable geometry, conflicting nets and malformed input
leave every old reservation intact. Release removes stale demand. Copies form
complete reservation snapshots, and `available_sites(owner)` exposes the free
gaps for replacing that owner's entire candidate. These are resource operations;
electrical/timing evaluation, congestion pricing, negotiation rounds, virtual
density and graph materialization still require integration.

Eleven hermetic site checks cover L/T-shaped and overlapping unions, partial and
zero-capacity gaps, multi-site competition, atomic replacement and rollback,
coordinate/count overflow and compressed trillion-site rows. An independent
physical-rectangle oracle checks 250 random unions; an independent per-site
ownership oracle checks 1,000 reservation/release updates.

`tree_search` expands a rooted physical routing tree into complete electrical
stages. Every supplied wire segment has explicit resistance, wire capacitance
and length. Inserting a buffer preserves the segment capacitance once and adds
actual directional RC for its physical input/output attachment wires. Every
original sink stays connected. Cell alternatives at one vertex may share an
occurrence identity; different simultaneously active vertices cannot.

Each complete finite site/cell assignment uses the common native waveform
evaluator with actual upstream slew and fresh pin loads. No C/Q-only pruning of
partial trees, slew discretization or fixed buffer gain is used. The full root
frontier compares physical rise/fall input requirements, all original sink
arrival/slew states, area, wirelength, normalized multiplier cost, symbolic
demand and exact occupied sites. Distinct competing-site choices remain
incomparable. Timing ties prefer strictly smaller resource demand over an
earlier equivalent choice. Shared reservations are checked in copied state;
search commits no reservations or graph edits.

Critical selection maximizes the worst physical input margin. Outside-window
selection minimizes area then wirelength only after the caller's physical
window requirements pass. The explicit short-net policy retains an electrically
feasible direct connection; a cap/slew violation still forces real search.
Assignment, frontier, waveform/node and elapsed-time bounds retain partial
feasible evidence with a distinct budget-exhausted status. Time is checked
between complete evaluations; one bounded evaluation can cross the deadline.
Missing/failed models cannot prove infeasibility. A complete result covers only
the supplied finite geometry and current input/timing context, and a deliberate
direct-connection early return does not claim exhaustive search. Whole-graph
timing/rank/coverage and physical checks remain mandatory before materialization.

The candidate context carries the same independently disabled characterized and
bounded driver options as native GP/reference/final timing. Each result records
the policy and every admitted driver mode/failure cause. These models do not
certify independent fidelity or electrical signoff; strict defaults are retained.

Eleven search checks compare all 120 fanout 1–4 alternatives and the complete
frontier with independent stage equations and an exhaustive oracle. They cover
critical isolation including every off-window pin load, actual cascaded input
slew, short-net cap violations, physical attachments/cap conservation, shared
site ownership, outside-window selection, unsupported models, finite-space
infeasibility and explicit budget exhaustion. This finite search is a useful
producer/reference for scalable DP; it does not complete E's van Ginneken,
extended-Hanan/Chen–Pedram–Buch, BEN or early-estimate calibration requirements.

`geometry` builds a bounded integer-DBU orthogonal visibility graph from terminal,
optional attachment/site and outer obstacle-corner coordinates. Hard routing
exclusions are separate from placement masks; their closed union, requested
clearance and one-DBU outer lines prevent artificial zero-width passages through
touching obstacles. Blocked terminals remain explicit. Original sink identities
survive duplicate coordinates. Input/grid/edge/obstacle-work/time limits and route
work limits distinguish unfinished work from a fully disconnected geometry.

A bounded LRU cache keys the normalized geometry, preserving terminal order and
invalidating moved sites, pins and obstructions. RC stays outside this cache.
A deterministic root shortest-path union prunes unused branches and supplies
`Routing_tree` to the existing native candidate/search evaluator. Every segment
gets the caller's actual directional RC, with exact wire-capacitance conservation.
No routing layer, crossing capacity or upper-layer RC is inferred. This is one
conservative lattice topology, not Chen–Pedram–Buch topology enumeration,
van Ginneken DP, BEN rerouting or a Steiner/timing optimum. Production phase,
virtual-buffer, shared-gap negotiation and materialization integration remain.

Twelve geometry checks include 200 independent unit-grid BFS comparisons,
fanout 1–4, touching/overlapping unions, clearance, narrow corridors, blocked and
coincident terminals, cache normalization/eviction/bounds, overflow/physical-unit
conversion and real native-library/site-search integration. The latter rejects
both an overloaded direct connection and a buffer whose input overloads the
source, then rejects an otherwise feasible choice when another net owns its site.
All 48 focused buffering checks pass. The optional explicit-input
`//tools:buffer_geometry_probe` profiles geometry separately from timing/search;
benchmark files remain outside default tests.

`graph_search` now applies sink-subset/reachable-set extension and branch merging
on the visibility graph. Partial branches may intersect only at their common
root; extending through a visited vertex is forbidden. The source cannot become
an internal vertex of a partial tree rooted elsewhere. Coincident sinks keep
all original identities and capacitances. Canonical edge sets remove duplicate
construction orders. These structural recurrences follow the topology-state
idea in Chen–Pedram–Buch,
[Buffered Routing Tree Construction Under Buffer Placement Blockages](https://mpedram.com/Papers/buf-route-blockage.pdf).

Every complete topology uses the existing discrete site/cell search and actual
native NLDM input slew. Root alternatives share one electrical/resource frontier.
A shortest-path seed provides early feasible evidence, but neither an unsupported
model nor an exhausted state/fit/time budget proves infeasibility or optimality.
The total assignment and waveform limits span every topology. State count,
reachable-set storage, transitions, complete trees, rejection counts and root
frontiers are explicit. Missing models retain their reasons. Searches commit
no shared reservations or graph edits.

No cap/Q-only partial-tree pruning or sampled-slew dominance is used: the actual
upstream waveform context is unknown until the complete candidate is evaluated.
This conservative exact finite-graph reference can be exponential. Scalable
NLDM-aware CPB/VG selection, BEN and unrestricted placement remain unfinished.
Twelve graph checks match an independent exhaustive edge-subset oracle and full
native frontiers at fanout 1–4, cover actual slew-dependent cell-choice reversal,
shared-resource/model/work-limit failures and a joint topology with less wire
and better required time than the shortest-path seed. All 60 buffering cases pass.

The explicit-input probe can additionally profile graph search from supplied
source input arrival/slew and sink required times with declared driver policies.
Strict defaults remain. Its real GP20 profiling mode supplies no physical buffer
options: it measures alternate unbuffered topologies, not buffer materialization,
whole-design rank changes or routed timing. Both saved CNN/litepci cohorts retain
blocked endpoints, missing timing contexts, electrical failures and state limits.

`physical_sites` connects original physical rows, current component poses and
explicit extra physical-only masks to the shared site inventory. All placed
components occupy space by default; the separate fixed-only mode is virtual
and does not certify materialization against movable cells. LEF/DEF units,
original row SITE/pitch/height/orientation, fixed geometry and associated
blockages are checked. Soft blockages are conservatively excluded. Macro halo
and legalization margin are separate explicit parameters. This does not prove
that a supplied snapshot contains every physical-only body; callers must supply
and audit that inventory.

Actual single-input noninverting NLDM buffers receive their LEF footprint and
physical pin offsets using the native first-shape centroid/origin convention.
N/FS row orientation, real symmetry and SITE/height compatibility determine
placement. Whole-site spans reserve the rounded-up width; this conservative
resource model can omit otherwise clear fractional-site gaps. Both physical pin
attachments use the evaluator's X-then-Y wires and separately supplied closed
routing exclusions. Placement masks never imply routing capacity or upper-layer
RC. Shared owners stay unavailable; own footprint replacement is reusable.

Options are bounded by explicit attachment radius, candidate/time limits and
per-vertex/per-library-type counts. Per-vertex exhaustion retains partial choices
there and continues other anchors. A partial option inventory cannot establish
an unrestricted search optimum or infeasibility. Returned row capacities are
constraints for the existing evaluator, not a second physical density charge.
No netlist edit or shared reservation commit occurs.

Fifteen physical-site checks include 100 independent dense site/routing-oracle
comparisons, N/FS pin origin/symmetry, stale masks, exact shared ownership,
fractional-width site demand, explicit bounds and DBU precision rejection. A
complete physical fixture passes real-library/shared graph search and rejects
an overloaded direct connection and wrong buffer type. All 75 buffering checks
pass. In the declared CNN/litepci GP20 profiles, both actual BUFx2 and BUFx4
alternatives are explored; 25 prospective CNN buffers across 19 independent
case alternatives and 9 litepci buffers across 9 alternatives pass a separate
original-row/fixed-mask/current-body/raw-LEF footprint audit. Cohorts can repeat
nets, and their alternatives are not allocated together. Cross-net negotiation,
production checkpoint refresh, full-graph timing/rank acceptance, virtual density,
transactional materialization and routed acceptance remain pending.


`negotiation` freshly evaluates supplied alternatives in their native source
contexts, then allocates exact whole-site spans and explicit integer crossing
resources against one ledger. Critical/window nets precede outside-window nets;
mandatory choices within the supplied set, previously unmet nets and severity
control order within a class. Physical input requirements rank critical choices
before prices. Outside-window choices minimize normalized area plus congestion
charge only after their physical requirements pass. Prices never relax slew,
capacitance, routing obstructions or zero capacity.

Current demand and bounded history price original legal gaps and explicitly
characterized resources. Exact interval overlaps drive gap history even when
aggregate row capacity would appear sufficient. Managed owners are replaced
atomically in a returned proposal; protected owners remain reserved, missing
contexts/options retain their old reservations, and explicitly retired owners
release demand. Geometry epochs reset stale prices. Round/candidate/waveform/time
limits return explicit unfinished work and preserve the original state. Early
limits retain every owner identity and the complete upstream-incompleteness flag.

Reservations and bounded gap-entry pulls are returned together. Pull targets are
the first intersection of an actual rectilinear driver-to-buffer-input RC path
with its original legal gap, including coincident source/input locations. A net's
pull fractions sum to at most its configured bound. Missing entries are reported.
The result never edits a netlist or placement database. Stable allocation covers
only supplied alternatives; it does not prove an optimum, full-graph source-arc
rank stability, routing-layer feasibility, or whole-design timing acceptance.

Sixteen hermetic negotiation checks cover competing mandatory nets, alternate
slots/unmet promotion, priority/window changes, epoch resets, price steering,
physical requirements and hard electrical/routing limits, unknown/zero/competing
crossing capacity, protected/retired owners, partial search and work bounds,
actual gap-boundary crossings, coincident roots and invalid-input atomicity.
The explicit-input probe optionally accepts `negotiation` with one declared
`cohort`, `max_rounds` and `max_seconds`. It uses distinct prospective cell names
and shared reservations for that cohort, retaining missing contexts and an
unbuffered seed. These real-net profiles explicitly classify all supplied nets
as critical diagnostic cases; production whole-graph window/LR classification,
density/force activation, graph/pool transactions and sizing remain pending.


`gap_forces` binds negotiated stage-zero pulls to actual physical driver pins
and one complete native `PinTiming` checkpoint payload. Targets convert from
microns to design units; fractions share the driver's original sum of sink
weights. A critical/window target can suppress straight pairs directed away from
its gap at the checkpoint. This suppression is frozen, so the objective remains
differentiable between checkpoints. Outside-window targets retain straight
pairs. Rebuilding the payload removes obsolete targets immediately. Unbound,
virtual-stage, fixed and inactive drivers are counted explicitly; fixed sources
never suppress useful movable sink forces. No virtual-stage force is silently
redirected to an original physical driver.

The owning DREAMPlace API 3 implements target weighted-average distance with
analytic gradients in its existing per-pin timing kernel. The same typed payload
already participates in atomic updates, continuation, topology clearing and full
optimizer checkpoint/rollback. The published dependency now supplies this API. Older dependencies
report the missing gap API explicitly; local overrides remain available for
native co-development. Native checks cover finite differences, an independent
two-pin WA oracle, units/translation, priority, validation, real GP movement,
clearing, continuation and exact resumed trajectories after rejection.

The optional probe's `gap_force_validation` flag binds one declared cohort at
its exact saved component poses and ownership, preserving original fixed/non-CORE
identity. It checks real-cohort gradients with uniform per-net sink weights.
This is a force diagnostic, not production LR/window classification, a density
solve, physical movement or accepted timing optimization. Production reservation
charge, filler reconciliation, phase/checkpoint refresh and inactive-buffer pool
activation remain pending; native force support alone does not complete G.

`density` prepares a complete typed native reservation payload from the current
negotiation proposal. It verifies exact site-ledger agreement, physical body
coverage and unique buffer ownership before converting units. Successfully
replaced or explicitly retired nets release their previous footprints; unmet
and unmanaged owners remain protected. Every materialized instance needs a fresh
explicit physical-node binding. This pure operation does not edit the graph or
certify source-context/rank changes.

The owning native reservation API deposits virtual footprints through the same
cell-density kernel as real cells. It never also subtracts their capacity.
Materialized bindings contribute only through their actual physical node, and
both virtual and materialized managed charge displace equal filler charge,
independently of the filler-budget fraction. Actual routability inflation is
included in that accounting; critical-buffer inflation caps still need production
policy integration. Insufficient filler charge is a reported rejection, not
negative filler mass. Local density targets and RUDY capacity stay unchanged.
Reservations and normalization arrays participate in checkpoint rollback;
continuation rejects out-of-checkpoint changes. Physical overflow stays available
separately from the solver metric including virtual reservations.

Ten owning native checks cover density-kernel/rectangle oracles, charge
transfer, actual GP motion, final checkpoint updates, exhaustion, continuation,
atomic failures, exact resumed rollback and physical activation/removal fixtures.
Eight owned binding cases cover site/body/identity checks, units, protected/unmet
owners, retirement, required-time policy and actual native GP charge transfer.
The published dependency now supplies reservation API 2. Older dependencies
report unsupported without claiming density activation.
The optional explicit-input probe can audit exact/smoothed native deposition for
real negotiated cohorts. These diagnostics do not run a full benchmark density
solve, move cells, materialize logical buffers or certify timing/routed QoR.
Production phase/LR/checkpoint refresh, pool/graph transactions, critical inflation
policy and full-graph timing/rank/electrical acceptance remain unfinished.

`checkpoint` now prepares timing weights, gap pulls, site ownership and reserved
charge together. It derives critical paths from current feasible predecessors,
uses the pin bank's exact window/temperature settings for shared LR mass, and
refreshes source input arrival/slew and every original sink requirement from the
same native result. Sink membership, masters and physical pin locations are
checked; missing or ambiguous source contexts remain unsupported. Off-window
requirements use the physical window target, separately from electrical limits.
Unmet/unmanaged owners retain their paired pulls and density demand. Replaced or
retired owners release both. Protected slots invalidated by new geometry reject
the proposal; no capacity is manufactured. Epoch/iteration checks reject stale
host state. This does not yet support arbitrary host graph/pool transactions.

The opt-in C++ `Options.native_gp_buffering` attaches that transaction to the
actual final-cell GP callback. A native geometry/alternative producer receives
fresh native timing, pin weights, RC, positions and spent iteration. Physical
preparation runs after native comparison/hard-period gates and before timing
policy commits; an exception cannot advance calibration, LR history, period or
response latches. Native rollback continues to pair the retained host allocation
with the retained optimizer payload. Reservation-aware solver density guards use
the owning API 2 metric; physical-only overflow remains separately available.
The published dependency supports this opt-in mode; older pins reject activation
explicitly.

Seven owning checkpoint cases include actual native GP/session refresh and the
public LiveHD placement entry with three nonempty joint updates, plus source/LR
refresh, protected ownership, retirements, malformed/stale contexts and epochs.
Two new timing-session cases verify failed preparation and skipped rejected or
deferred frames. The initial small GP fixture's unavailable near-critical ranking
is retained as a failure; a non-tied two-state near-critical fixture now exercises
the unchanged gates. The public entry additionally caught missing physical master
metadata, now supplied from the exact current checkpoint view.

The explicit-input probe can replay one joint checkpoint on a full original
signal graph and saved wire RC, with fresh native timing/LR state and declared
integer-DBU rounded endpoint geometry. Its earlier all-critical diagnostic remains
separate. These replays do not establish continuous/Steiner fidelity, GP movement,
logical materialization or routed improvement. Production phase-policy actions,
a scalable in-memory geometry/alternative producer, critical inflation caps,
host pool/graph transactions, whole-graph buffered timing and full acceptance
remain pending. Synthesis/resynthesis stays excluded.

The first fresh full-native GP20 replay changes the interpretation of the old
32-net clear-endpoint cohorts: CNN has no current critical/window nets there;
litepci has one window net and no critical nets. The all-critical 13/9-buffer
diagnostics therefore do not represent current policy demand. Fresh shared
window/source context retains four CNN buffers and no litepci buffers in those
finite cohorts. These observations motivate selecting actual critical/window
nets in the next geometry producer. They neither prove whole-design demand nor
complete bounded searches, which remain explicitly incomplete.


`producer` now selects actual current critical/window RC owners from native
worst paths and the shared LR bank, plus previously managed owners. Selection is
bounded, deterministic and records omitted owners and unsupported source contexts.
The same fresh context binder is used before negotiation, including all original
sink identities and physical input arrival/slew/required times. No saved cohort
classification or caller-supplied timing requirement is trusted.

The in-memory producer rebuilds original-row legal inventory with current master,
orientation and component metadata and physical-only masks. Occupied movable
snapshots include at least one DBU exclusion margin for rounded component origins.
Full-precision terminal pins attach to the integer visibility seed with explicit
rectilinear technology RC; terminal positions are never silently rounded. Routing
masks remain distinct from placement masks. Protected owners remain reserved and
geometry changes invalidate congestion history. Canonical physical buffer
footprints/pins are checked against the actual library/LEF master.

This first producer searches one obstacle-avoiding shortest-path topology and
finite site/cell choices; every input search remains explicitly incomplete. It is
not scalable VG/CPB/BEN, layer/crossing capacity, buffered whole-graph timing,
physical materialization, or production seven-phase acceptance. Five optimized
hermetic cases check current owner discovery/unsupported contexts, actual joint
reservation admission, exact fractional terminal attachments and geometry errors. The fifth case runs the actual public placer, regenerates
current candidates three times and carries prior reservations between updates.
The diagnostic `joint_checkpoint_validation.generate_native_candidates=true`
requires explicit `directional_rc` and enables a fresh original-checkpoint replay;
macros are opaque in that diagnostic, with no invented over-macro routing RC.
