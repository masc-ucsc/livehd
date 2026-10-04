# USYN2 — native multi-representation logic optimization for pass.usyn

Status: reviewed plan (2026-10-03); no implementation authorized by this review.
USYN already has endpoint selection, residual rewriting/resubstitution and one
feedback round. USYN2 proposes extensions and replacements described below;
file names and new options are provisional until their milestone is implemented.

Summary: USYN keeps one shared XAG as its working network. Each admitted cone is
re-expressed in up to four local representations (XAG, bounded mux/BDD, SOP,
majority), optimized there, converted back into the shared XAG by hash-consing,
and kept as an alternative (a choice) instead of a replacement. A final selection
picks a consistent set of alternatives using technology-aware cost estimates.
Placement has a light cleanup before unate (DOMINO) endpoint selection and full
multi-representation optimization of the residual after selection. For CMOS, a separate final
pass also optimizes the expanded selected functions (§3.11). The design is
inspired by mixed structural choices (MCH, §8), but native selection before
mapping is a material difference from mapping with choices.

Ground rules carried over from the [USYN redesign](../../todo/livehd/2u-usyn.html):

- USYN builds and runs with zero ABC. Only the optional technology-mapping step
  (`pass/synth/tmap.hpp`, ABC provider `pass/abc/abc_tmap.cpp`) may use ABC, and
  only for technology mapping, buffering and gate sizing. No ABC logic synthesis,
  fraiging or fallback in USYN. External ABC ablations and LEC are validation
  tools, not part of the native optimizer.
- No runtime SAT/SMT prover in USYN. Use exhaustive truth tables on bounded
  independent supports and validated algebraic identities/structural transforms.
  Wider transforms need compositional justification (§3.8); random simulation
  never authorizes a replacement.
- Target near-linear traversal with fixed local limits; do not claim worst-case
  near-linear behavior for graph mutation or candidate search. All work and peak
  memory are bounded and reported (§4.4); results are deterministic at fixed
  input, options and work credits.
- Compact loops stay rolled (ruling 2026-10-03); this plan works on the
  per-region logical network and never expands a loop.

## 1. The problem, measured

Historical snapshot: `../lhdtrack/target/report-satsuma.html` (asap7,
`pass.satopt=false`) reported 151 paired rows, USYN/ABC geomean area 1.028 and
delay 1.012, with 63 area and 62 delay regressions. Preserve the tables below as
diagnostic observations, not a newly reproduced baseline.

The archived `../lhdtrack/target/usyn-under-0.8-satsuma.json` for synthesis run
`20261003T120458` records **153 positive area pairs and 152 positive delay pairs**:
ABC/USYN geomeans 0.972742 and 0.989948, respectively (reciprocals about 1.0280
and 1.0102). There are 18 area and 20 delay ratios below 0.8. These metric-specific
cohorts differ from the historical 151-row cohort. A ratio below 0.8 in that
report means USYN is more than 1.25× the ABC value, not 20% worse. Do not mix
cohorts or silently omit timeouts when claiming improvement.

Worst cases (mapped area µm² / delay ps):

| test | lhd abc | lhd usyn | ratio |
|---|---|---|---|
| br_arb_rr | 16.4 / 389 | 34.2 / 395 | area 2.08x |
| br_flow_arb_rr | 17.2 / 371 | 35.4 / 404 | area 2.06x |
| br_mux_bin | 32.8 / 217 | 67.0 / 207 | area 2.04x |
| br_mux_bin_array | 66.6 / 214 | 134.0 / 207 | area 2.01x |
| icmp | 3.0 / 141 | 5.0 / 151 | area 1.65x |
| comparator | 2.3 / 113 | 3.8 / 122 | area 1.64x |
| br_flow_arb_fixed | 2.1 / 97 | 1.8 / 204 | delay 2.12x |
| br_demux_bin | 3.7 / 57 | 3.7 / 115 | delay 2.02x |
| br_arb_multi_rr | 45.4 / 457 | 38.9 / 807 | delay 1.77x |

### 1.1 Mapping-only comparison (diagnostic, not a controlled causal result)

The ABC flow was rerun with USYN's tmap script substituted
(`--set "pass.abc.flow=strash; write_aiger X.aig; &get -n; &nf {D}; &put -o; buffer -N 16; dnsize {D}"`):
lhd's own blasted network, mapped with no technology-independent optimization
("abcbare"). Combinational regions only, ABC static timing:

| test | abc | abcbare | usyn |
|---|---|---|---|
| br_flow_arb_fixed | 2.11 / 117 | 1.76 / 211 | 1.76 / 211 (equal reported metrics) |
| br_arb_rr | 11.7 / 311 | 35.5 / 395 | 29.5 / 317 |
| br_flow_arb_rr | 12.5 / 292 | 36.4 / 409 | 30.8 / 340 |
| comparator | 2.30 / 117 | 4.52 / 157 | 3.78 / 132 |
| br_arb_multi_rr | 40.8 / 392 | 37.2 / 700 | 34.2 / 731 |
| br_enc_priority_dynamic | 7.8 / 285 | 11.1 / 485 | 9.8 / 438 |
| br_cdc_fifo_ctrl_push_1r1w | 16.8 / 301 | 22.7 / 595 | 19.6 / 520 |
| br_flow_join | 5.2 / 166 | 7.1 / 319 | 7.1 / 232 |
| br_mux_bin | 32.8 / 145 | 33.9 / 202 | 67.0 / 214 |
| br_demux_bin | 3.72 / 77 | 3.78 / 78 | 3.77 / 130 |

The table shows 10 of the 12 local experiments. The omitted `icmp` result is
ABC 3.0472 / 157.6652, abcbare 4.4323 / 187.6071, USYN 5.0155 / 168.4122;
`br_flow_mux_rr` is 13.0491 / 256.8222, 19.4935 / 209.1422, 18.9540 / 226.6197.
USYN matches or beats abcbare **area** in 10/12; this is not simultaneous area
and delay dominance. Partitioning, endpoint selection and native optimization
also differ, so these runs do not isolate any one cause.

USYN already optimizes the residual; the question is why its bounded operators
leave these opportunities. The archived residual/feedback ablation reduces
`br_arb_rr` area from 39.045 (selection only) to 33.957 (residual), then 34.161
(with feedback), while `br_mux_bin` remains 66.981 throughout. This supports
improving residual coverage and measuring feedback independently, rather than
assuming either stage has no effect.

### 1.2 Script ablation on dumped networks (reproduce scope per §1.4)

| step | br_arb_rr | comparator | br_arb_multi_rr | br_enc_priority_dynamic |
|---|---|---|---|---|
| bare `&nf` | 33.1 / 425 | 4.52 / 147 | 21.4 / 702 | 10.3 / 472 |
| `balance` | 27.5 / 330 | 4.67 / 152 | 20.7 / 713 | 8.9 / 460 |
| `dc2` | 16.5 / 381 | 3.22 / 158 | 18.9 / 757 | 8.1 / 479 |
| `fraig; dc2; &dch` | 13.2 / 270 | 2.30 / 112 | 19.0 / 841 | 7.8 / 605 |
| `&synch2; &if -m; &mfs` | 16.1 / 296 | 3.70 / 171 | 30.8 / 542 | 9.4 / 244 |

AIG sizes before and after:

| test | input and/lev | fraig | balance | dc2 |
|---|---|---|---|---|
| br_arb_rr | 923 / 37 | 803 / 36 | 830 / 14 | 343 / 22 |
| comparator | 94 / 18 | 59 / 16 | 94 / 17 | 54 / 11 |
| icmp | 125 / 21 | 63 / 18 | 122 / 20 | 79 / 14 |
| br_flow_join | 182 / 16 | 182 / 16 | 124 / 16 | 107 / 12 |
| br_enc_priority_dynamic | 225 / 46 | 225 / 46 | 148 / 35 | 120 / 46 |

These cases motivate both area rewriting and timing-oriented restructuring.
Individual stages can improve one metric and regress another; the table does
not establish a universal recipe or that bounded truth-table sweeping can match
SAT-based `fraig` on arbitrary supports.

### 1.3 Per-endpoint cone depth (rtl network vs USYN network, AIG levels / cone ANDs)

| endpoint | rtl | usyn |
|---|---|---|
| br_flow_arb_fixed push_ready[k] | 5 / 18 | k / k (ripple) |
| br_arb_rr grant[k] | 14 / 243 | 28+k / 176 |
| br_arb_multi_rr grant[15] | 24 / 1281 | 47 / 379 |
| br_flow_join pop_valid | 7 / 79 | 12 / 31 |

Some measured endpoints trade substantially fewer gates for greater depth.
Neither logic levels nor maximum per-region ABC delay is whole-design STA.

### 1.4 Reproducibility prerequisite (M0)

Archive each experiment's source/configuration, binary and dirty-source hashes,
Liberty content hash and units, full commands, reader, clock/load constraints,
region boundaries, ordered boundary signals, input AIG hash and raw logs. Give
each row its region/top scope; totals and the maximum region delay cannot be
compared directly to whole-design timing. ASAP7 here declares `time_unit=1ps`;
record the area convention and normalize other libraries explicitly.

The local scripts in `/var/tmp/renau/usynab/` are useful evidence, not reusable
harnesses yet. `real.sh` writes every mapped region to the same `lhd_in.aig`
path (potential overwrite); `ab.sh` round-trips through Yosys and uses a sizing
recipe different from `real.sh`. Use unique per-region dumps and pin an
identical mapping/sizing recipe before asserting an exact-input comparison.
Retain these historical runs, then reproduce controlled pairs. Report the
original full ABC baseline, the same-input mapping-only control, and each USYN
ablation separately. Shared blaster changes require a newly measured ABC baseline
alongside the frozen baseline, not substitution of one for the other.

## 2. Issues this plan must address

| id | issue | where | evidence | fixed by |
|---|---|---|---|---|
| I1 | tmap maps with `strash; &nf` only: no rewrite, sweep, choices or delay resynthesis, and no area/delay candidate pair | `pass/abc/abc_tmap.cpp`; USYN lacks comparable breadth | abcbare ≈ usyn (§1.1); ablation (§1.2) | M3–M7 |
| I2 | `arith::build_eq` folds bit compares left-deep; strash merges `x[k-1:0]==0` prefixes into one serial chain | `pass/synth/arith.hpp:589` (shared by both flows) | push_ready[k] depth = k; arb_rr input lev 37, `balance` 14 | M0 (source), M3 (balance) |
| I3 | audit compare lowering for redundant extensions; do not assume guard bits are redundant | `pass/synth/blast.hpp` EQ/LT lowering | `fraig` reductions motivate an audit, not proof that width is wrong | M0 (source), M4 (sweep) |
| I4 | indexed multi-arm mux uses decode-AND-OR; suspected loss of shared decode during flat mapping (NAND5 per data bit in br_mux_bin) | `pass/synth/blast.hpp` Mux lowering; USYN single-region tmap | br_mux_bin 786 gates vs 375 for the same function; 682 even area-only; `dc2` leaves it at 1020 ANDs, `&dch` choices recover 33.8 µm² | M0, M1, M2, M7, M8 |
| I5 | the sampled endpoint-free designs report 0 rewrite / 0 resub wins | `pass/usyn/residual.cpp` | comparator: 5.7M work, 0 wins, while `dc2` finds -43% | M2 + M5 replace it; M0 adds the missing counters |
| I6 | the XAG is append-only with external ledger pricing; test whether mutable liveness enables better search within the same bounds | `pass/usyn/xag.hpp` | `rewrite_root` takes one window per root; 4-input templates only | M2 |
| I7 | current AND/XOR/mux proxy costs may misrank CMOS candidates; calibrate against legal target cells | `Residual_options` | cell ratios depend on legal drive, phase and load; verify with Liberty-derived tables | M2, M9 |
| I8 | delay-oriented resynthesis is missing (`&synch2; &if -m; &mfs`) | — | arb_multi_rr 702→542 ps, enc_priority_dynamic 472→244 | M6 (SOP balancing) partly, M10 |
| I9 | a single representation biases the mapped result: whatever structure reaches the mapper largely dictates the mapped netlist (structural bias), and the current bounded rewrite set may not discover useful mux-tree or majority structures | whole USYN optimization design | in the sampled recipes `&dch` helps br_mux_bin while `dc2` does not | M6, M6b, M7 |
| I10 | in the CMOS target, selected unate functions keep an SP form chosen for DOMINO transistor cost, which need not be the CMOS optimum | `pass/usyn` CMOS expansion | cost objectives differ; isolate expansion in an ablation | §3.11 |

Corrected earlier diagnosis: `import_lnet` (`pass/usyn/xag_lnet.cpp:73`) Shannon-
expands each Lnet node in fixed fanin order. In the inspected mux network the
nodes are two-input, so this alone does not explain I4. Lnet can represent wider
functions; do not generalize that observation to all imports. The partitioning
and decode-sharing explanation in I4 still needs the controlled §3.8 ablation.

## 3. Design

### 3.0 Correctness and ownership prerequisites

- Maintain an immutable accepted incumbent. Build and validate a candidate in a
  bounded transaction/scratch arena before publishing it. Exhaustion or
  cancellation keeps the last complete valid design; invalid candidates are
  diagnosed, never silently accepted. Reserve work for validation and export.
- A truth table is tied to an **ordered independent leaf basis**, output phase,
  graph generation and any care relation. P2 global replacements must be equal
  for every assignment on that basis. An existing endpoint completion valid only
  on a reachable image cannot become an unconditional equivalence-class member;
  preserve its domain restriction and revalidate full composition.
- Preserve the full boundary manifest: top ports and bit order, register and
  memory hierarchy/bit identities, D/Q bindings, clock/domain/edge, enables,
  reset/init semantics, memory and ICG/opaque ports, and rolled-loop boundaries.
  No retiming, state merging, new phase storage or cross-clock rewriting.
  Internal combinational names may change; named semantic boundaries must rebind
  through the shared semdiff/LEC matcher. Names alone are not proof evidence.
- Graph IDs are not durable identities. Use generation-checked handles or an
  explicit rebinding map; invalidate saved windows, cuts, signatures, fanout/level
  data, endpoint proofs and cost records after affected mutations. Compaction
  must update every owner before old IDs can be reused. This is essential because
  today's append-only XAG deliberately keeps saved windows immutable.

### 3.1 Representation: mutable, mux-aware XAG

Extend `pass/usyn/xag.hpp`:

- **Node kinds**: `constant, source, and_gate, xor_gate, mux` (mux = `s ? t : f`,
  three fanins; canonical form keeps `s` uncomplemented and normalizes
  `s ? t : f` vs `~s ? f : t`). Hash-consed like AND/XOR.
- **Fanout reference counts** maintained on every edge change.
- **`replace(old, new)`**: redirect every fanout of `old` to `new`, re-hash the
  touched fanout nodes, and cascade the merges that re-hashing exposes (the
  ABC/mockturtle "substitute with strash" pattern; inspiration only, no code or
  dependency).
- **MFFC deletion**: when a node's reference count reaches zero, delete it and
  decrement its fanins recursively. `mffc_size(n, cut)` is computed by
  a scratch reference-count view (or guaranteed restoration on every exit), so
  pricing has no observable mutation. Separate selected-network references from
  choice-retention references; otherwise unused alternatives prevent deletion
  and distort gain. Reject replacement dependencies on the old root or its
  transitive fanout, even when a hash lookup finds them.
- **Levels**: updated incrementally on replace (forward propagation through the
  changed transitive fanout, bounded); full recompute at pass boundaries.
  Reference counts alone cannot locate consumers: maintain fanout edge lists or
  a bounded reverse index. Rehashing, merge cascades and level propagation must
  commit atomically; budget exhaustion cannot leave stale hash keys or cycles.
- **Compaction** at the end of each pass, preserving source order and output
  names (the residual interface contract stays).
- **Choices** (§3.9): an optional equivalence-class link per node.
- The shared graph keeps only AND, XOR and mux nodes. Majority, SOP and BDD
  forms live only inside the per-cone operator (§3.6) and enter the graph
  expanded to XAG.
- **Protected outputs**: domino inputs, POs and special-state ports are pinned
  root handles that no pass may remove or reorder. Equivalent internal drivers
  may share; preserve each observable port and its metadata. Physical DOMINO
  boundaries and CMOS expansion have different policies (§3.11).

For one committed replacement, proxy gain is weighted live logic freed minus
newly live logic required, including demanded inverter rails and external
readers. A hash hit on a dead/unselected node is not free; resurrecting its cone
has a cost. Reprice after overlapping replacements and after selecting choices.
Use MFFC accounting for static logic, while preserving the existing full mixed
network accounting for shared DOMINO cells, rails and storage. MFFC alone cannot
replace that ledger or guarantee lower mapped area.

### 3.2 Balance (I2)

- Collect maximal **AND groups** through uncomplemented, single-fanout edges, and
  maximal **XOR groups** (XOR is associative; complements pull out as parity, an
  XAG advantage over AIG).
- Collect **mux trees** sharing a data path: a chain `s1 ? a : (s2 ? b : ...)`
  is a priority mux and can be rebalanced into a log-depth form when the
  selects are proven mutually exclusive on the valid basis, or by an explicit
  priority-preserving prefix/tree transform. Never infer exclusivity from sampled
  simulation; test simultaneous selects, default arms and complemented controls.
- Rebuild each group by arrival-time pairing (Huffman on levels): combine the two
  earliest arrivals first.
- **Bounded duplication across shared nodes**: the `build_eq` ripple is made of
  shared prefixes, and a plain balance stops at multi-fanout nodes. Allow
  duplicating a shared node when it is on the critical path, its MFFC is below a
  limit, and the depth gain is at least one level. The diagnostic target is the
  per-output depth seen in the RTL path (push_ready[k] 15→5 levels).
- Two modes: depth-guarded (accept estimated timing improvement within an
  explicit area/duplication allowance) and area-neutral (nonincreasing weighted
  live cost). Unit-depth Huffman pairing is a heuristic when cells, pin arrivals
  and fanout loads differ; preserve the incumbent when it does not improve.

### 3.3 Equivalence sweep (I3)

- Random simulation, many 64-bit words per node (reuse the existing window
  simulation), bucket nodes by signature and complemented signature. Fix and
  report the PRNG seed; cap bucket size and pair attempts to avoid quadratic work.
- Confirm a candidate pair exactly when the union of their supports, or a common
  cut, has at most 16 inputs, using the multi-word truth tables already in
  `function.cpp`. A common cut must bound both cones with identical leaf
  identities/order; compare all assignments, not just reachable samples.
  Unconfirmed pairs stay separate (sound, conservative).
- Merge confirmed pairs with `replace`, keeping the shallower representative.
- Refine local-basis buckets with counterexamples from failed confirmations.
  A cut assignment is not necessarily realizable at primary inputs: only replay
  a global pattern if a valid input preimage is known. No SAT preimage search.

### 3.4 Rewrite (cut-based, library-backed) (I1, I5, I6)

- **Cut enumeration**: priority cuts, up to 8 per node, at most 4 leaves, with
  cut truth tables (16 bits) computed bottom-up.
- **Canonicalization**: NPN class of the cut function (222 classes for 4 inputs).
- **Library**: precomputed offline by bounded exact-synthesis searches under
  the XAG(+mux) cost model, checked in as a generated table (`xag_npn4.inc`), with 2–3 structures
  per class (area-best, depth-best, mux-using). The generator is a separate
  build-time tool, not part of `lhd`; the table carries its own version/salt.
- **Selection**: for each node and cut, evaluate each library structure, count
  nodes that already exist (hash lookup before creating), and take the best
  gain. Accept gain > 0; with the zero-gain mode (ABC's `rewrite -z`), also
  accept gain = 0 when depth does not increase, to escape local minima.
- Topological order, transactional `replace`, bounded attempts per root/version and per original root across a round.
  Zero-gain moves use a deterministic tie-break and revisit cap; avoid rewrite
  oscillation across rounds. Validate NPN input permutation, input/output phases,
  constants and degenerate support on all 65,536 four-input truth tables.
- Record the generator version, reproducible command, cost model and per-entry
  optimality certificate/status. A bounded search result is best-known, not
  necessarily optimal; a small library is not optimal for every Liberty file.

### 3.5 Resubstitution (I1, I5)

- Keep the current divisor collection and simulation filtering; price with real
  MFFC gain; 0-, 1- and 2-node resub with AND, OR, XOR and mux of divisors.
- Retain existing limits initially; raise them only after a measured benefit.
  Enumerating muxes or pairs of inserted nodes can grow combinatorially in the
  divisor count. Bound attempts and exact validation separately, and reject
  divisors in the root's transitive fanout (§3.1).

Refactoring is no longer a separate XAG-only step: it is the multi-representation
operator of §3.6, which subsumes ISOP refactoring.

### 3.6 Multi-representation local optimization (I1, I4, I8, I9)

The sampled `&dch` result and the related work (§8) motivate exploring structural
alternatives. They do not establish that this particular bounded recipe or its
pre-mapping selection will recover the same result. Each admitted cone may
produce several independently validated candidates; retention is bounded and
must pay for itself in ablations.

**Representations**

| representation | strong on | lhdtrack cases | construction and bound |
|---|---|---|---|
| **XAG** (exists) | general logic, XOR-heavy logic (parity, compare) | comparator, icmp | rewrite (§3.4), resub (§3.5) on the shared graph |
| **Mux / ITE network; bounded ordered BDD locally** | selection, decoders, priority chains | br_mux_bin(_array), br_demux_bin, arbiters | Shannon expansion with a variable-order heuristic (select-like/control variables first, chosen by DSD); a BDD requires one fixed variable order on every path, a unique table and elimination of nodes with equal children; a general shared mux DAG is not a BDD; cap nodes, memo entries and work |
| **SOP / factored form** | two-level structure, depth (SOP balancing, ABC `&sopb`) | br_flow_join, br_enc_priority_dynamic | ISOP of `f` and `~f`, algebraic factoring, SOP balancing; truth-table windows of 10–12 inputs, symbolic SOP for wider mux-like cones (§3.8) |
| **MIG (majority)** | carries, comparisons, counters (comparator carry/borrow recurrences and full-adder carry use majority; signedness remains explicit) | comparator, icmp, br_credit_counter, popcount/adders | majority recognition from cut functions, a small majority-axiom rewrite set (associativity, distributivity), expansion back to XAG |

No AIG (the XAG subsumes it) and no unbounded BDDs (representation explosion).

**Decomposition helper: DSD.** Disjoint-support decomposition splits a function
into supported disjoint-support blocks from the ≤16-input truth tables. Start
with AND/OR and XOR/XNOR splits; leave unsupported prime blocks intact. Treat mux
decomposition as an additional checked heuristic: general mux cofactors can
overlap in support. Specify normalization and tie-breaks rather than claiming
canonical DSD for an unspecified algorithm. It is not a fifth network: it is
the front end of the mux and SOP builders (top split, variable order) and of
MIG recognition.

**Truth-table machinery.** USYN already has most of it in `function.cpp`
(multi-word tables to 16 inputs, exact support and polarity, minimal true points,
signed covers with redundant-cube removal, SP factoring). ISOP and DSD are added
natively there, so the unate analysis and the SOP representation share one
implementation; DOMINO transistor cost and CMOS cost remain separate. kitty
(header-only, listed as an evaluation item in the 2u plan) could be considered
for the offline NPN4 generator (§3.4), with no runtime dependency. This plan
does not settle the existing dependency decision.

**Per-cone operator.** For each admitted cone (reconvergence-driven window,
`grow_window`, at most 16 inputs; wider mux-like cones use §3.8):

1. Freeze the valid ordered basis and compute its function once. Wide mux
   candidates use the structural/compositional path in §3.8, not a full table.
2. Try bounded DSD/order heuristics; their failure does not suppress the original
   XAG candidate or all other representations.
3. Build candidates in bounded scratch storage: XAG rewrite, mux-BDD, factored
   SOP and, later, MIG. Cap total scratch bytes across all representations.
4. Validate equality to the original function on the same basis, then translate
   into a transactional XAG overlay using live hash-consing. Charge conversion,
   table evaluation and allocation; a rejected candidate leaves no live debris.
5. Price incremental selected-network cost (§3.1) and arrival estimates (§3.7).
   For competing/overlapping cones, these prices are provisional until selection.
6. Keep the incumbent and bounded diverse candidates (§3.9). Local area/depth
   dominance is only a pruning heuristic when sharing and fanout contexts differ.

Any representation that exceeds its bound for a cone (BDD node limit, SOP cube
limit, MIG rewrite budget) simply contributes no candidate; that is reported,
never an error.

### 3.7 Cost model (I7)

Use a versioned native cost model, with deterministic library-independent
weights when no Liberty file is supplied. A later Liberty-aware mode builds a
small cut/cell table from legal combinational cells, including NPN pin/output
phases, inverter cost and units. Do not assume a cell named `x1` exists or use
area and delay from different cells as an achievable pair. Reuse an ABC-free
Liberty reader; `dont_use`, supported functions and drive choices must agree
with the mapper's legality policy.

Separate weighted area from arrival estimates. Calibrate representative load
and slew points, pin-specific arcs and fanout penalties on ASAP7 and sky130.
Logic levels and intrinsic cell delay are ranking proxies, not STA guarantees.
Charge majority candidates after XAG expansion (no double counting), and charge
shared inversions only where demanded. Keep source-state/memory area outside a
pure combinational cost comparison, but include it in whole-design reports.

Library-aware selection changes native results: hash Liberty **contents**, cost
model version, constraints and effective policy into the native cache key, not
only the tmap key. Library-independent mode should retain library-independent
reuse. Define feasible/infeasible timing-target ranking before implementation:
prefer feasible lower-area candidates; if none meet the target, minimize delay
then area and report the missed target. With no timing target, rank by area
then delay. Final mapped measurements decide QoR.

### 3.8 Mux-aware lowering and shared decode (I4)

- Offer decode-AND-OR and binary mux-tree lowerings while retaining source mux
  metadata long enough for USYN to construct both. Lnet alone may have lost that
  intent; define an ABC-free annotation/sidecar and its cache identity before
  promising alternatives after import. Recognize bounded two-arm mux patterns
  in existing logic as a fallback.
- Preserve current semantics: two contiguous arms use a **nonzero predicate**
  for a wide selector, not `sel == 1`; indexed multi-arm muxes must retain sparse
  and out-of-range behavior, constant-arm pruning, width/sign extension and the
  existing unknown-value policy. Do not invent don't-cares or evaluate an
  unreachable self-hold arm. Priority muxes require the §3.2 rules.
- For wide cones (e.g. four selects plus sixteen data inputs), start from known
  mux structure or an already bounded cube cover. Recursively cofactor on a
  bounded control set with memoization; keep residual data expressions symbolic.
  Never first enumerate a wide truth table or expand an arbitrary DAG into SOP.
  Presence in every SOP cube is a heuristic, not a definition of a selector.
  Validate selector assignments against the exact residual expressions/identities;
  abandon a candidate whose residual equality cannot be established within the
  bound. No probabilistic acceptance.
- Track shared decode terms across outputs and include their full fanout/area
  effect in extraction. Prefer preserving useful sharing in the selected DAG
  before introducing mapper barriers.

**Mapping-boundary experiment, not a default fix.** The current `tmap.hpp`
contract explicitly says proposed DOMINO boundaries are not mapping barriers.
Forcing them would change the interface and can worsen timing or block useful
mapping. First compare flat mapping, explicit decode partitions and mux-tree
lowering on identical inputs/constraints, including buffer and sizing costs.
A fanout threshold alone is insufficient justification.

If the experiment succeeds, specify optional *combinational sharing hints* in
`pass/synth/tmap.hpp`, provider behavior and cache keys. They must not create
public ports/state, cut clocks or memory semantics, or turn logical DOMINO
boundaries into physical cells. Propagate arrival/load constraints across any
new partitions; compare final full-design STA. Implement provider support only
after this contract is documented and tested, with an unrestricted-map control.

### 3.9 Choices and selection (I4, I9)

- Each class contains unconditionally equivalent, phase-normalized functions on
  the same valid boundary (§3.0). Default cap 3 retains the incumbent and at most
  two diverse area/depth candidates; deduplicate identical structures. Different
  sharing/polarity patterns can matter even when local proxy costs are worse.
- Treat retained alternatives as immutable or copy-on-write. Mutation must not
  invalidate another member's function/evidence. Track class ownership separately
  from selected-network liveness. Check cycles at insertion, class merging and
  final extraction; creation order alone does not guarantee acyclicity.
- Selection is a bounded heuristic, not independent per-cone minimization.
  Compute bottom-up cost/arrival estimates, propagate required times from roots,
  then select and perform bounded area recovery with actual selected references.
  Recount shared nodes once, and reprice overlaps. Use deterministic tie-breaks;
  retain the valid incumbent if consistency checks or work limits fail.
- Produce at most two complete networks (area and depth-oriented), from the same
  immutable starting snapshot. Native selection before tmap cannot reproduce
  choice-aware mapping in general: the mapper no longer sees the alternatives.
  During evaluation, map both through the same mapping/sizing-only provider and
  record which proxy predictions survive mapping. Any production two-map policy
  needs an explicit shared runtime budget and measured benefit.
- Clear choice ownership and compact after extraction, rebinding all roots and
  evidence; validate the resulting DAG before export. No choices cross the
  current tmap interface. Defer a native choice-aware covering mapper to a
  separately scoped proposal if early selection remains the limiting factor.

### 3.10 Recipe and placement

**P1 — before unate endpoint selection: light and cheap.** Initial recipe:

```
mux-recognize; sweep; balance(area-neutral); rewrite(gain > 0)
```

P1 creates no choices. It may reduce support or change window admission, but
restructuring can also remove useful divisors; benchmark endpoint count, coverage,
search cost and final mixed-network cost with P1 on/off. Do not infer increased
admission merely from a smaller AIG. Until rewrite lands, use the available
prefix of this recipe and report its exact effective stages.

**P2-R — residual after selection: bounded full recipe.** Proposed order:

```
sweep; balance; rewrite; multi-rep; resub; balance; rewrite -z; select
```

Run bounded rounds, stopping on insufficient whole-network gain, repeated
fingerprints or budget exhaustion. Area/depth trials fork the same snapshot and
share one total allowance; “run twice” must not double the advertised budget.
Reserve validation/export work and fair minimum credits so early sweep/SOP work
cannot starve later representations. Report stages skipped for lack of credits.

P2-R incrementally replaces `optimize_residual` once equivalent-boundary and QoR
gates pass. It preserves selected-cell inputs/rail demands and keeps the existing
single affected-endpoint feedback round. Revalidate the mixed model after that
round; a second feedback round is deferred until a separate ablation justifies
its cost. No trial consumes the already-mutated output of the competing trial.

**P2-C — CMOS only:** after final freeze/validation and behavioral expansion,
optimize the complete combinational CMOS network, including expanded selected
functions (§3.11). It reuses the operators but has its own slice of the same
regional allowance. No endpoint feedback follows P2-C in the initial design.

### 3.11 Unate (DOMINO) functions and the two targets (I10)

Endpoint candidates are checked for exact function and legality under stack,
branch, polarity and clock constraints. The bounded search does **not** establish
global optimality of boundary selection, decomposition or care completion.
DOMINO transistor cost and CMOS library cost are distinct objectives.

- **DOMINO model / future physical target:** keep the validated frozen endpoint
  model authoritative. P2-R optimizes static residual logic while preserving
  selected functions, ordered inputs, rail demands, phases and storage owners.
  Physical DOMINO cells, electrical constraints and mapping remain outside this
  plan. Preserve `clock_phases=2` as the current default and test both 1 and 2;
  these are logical phases/cells, not extra CMOS registers.
- **CMOS target (today):** complete selection, P2-R and feedback, freeze and
  validate the endpoint model, then expand it with the existing writer. P2-C
  operates on a private complete CMOS copy. Expanded unate functions may be
  optimized across former combinational cell boundaries; their original SP form
  remains an incumbent candidate where useful. Original state, controls, memory
  and opaque barriers stay fixed. Never merely unpin selected-cell inputs in the
  mixed model: its care relations and static/phase invariants would be invalid.
- Preserve the frozen DOMINO artifact as the logical search result and record the
  CMOS transformation/provenance separately. DOMINO cost, CMOS proxy cost and
  mapped PPA must have distinct report fields. Global equivalence classes in
  P2-C use total composed functions; reachable-care completions cannot be moved
  outside their validated context. External LEC checks the final emitted netlist
  against both source forms; internal validation checks every accepted transform.

This placement changes the earlier “all P2 before expansion” proposal deliberately:
residual feedback belongs before freeze, full CMOS restructuring belongs after
validated expansion. The writer's original next-state checks remain mandatory.

### 3.12 Compare-lowering audit (I2, I3)

Balance `build_eq` only after measuring sharing and depth together. Its repeated
prefixes serve multiple outputs; balanced local reductions may cost more area.

Do **not** remove `max(width)+1` or LT/GT guard bits as a blanket optimization.
The current EQ width distinguishes signed extension from unsigned magnitude;
LT/GT reserve a subtraction guard (e.g. signed 2-bit −2 versus unsigned 2-bit 3).
A narrower implementation needs a precise predicate proving redundant extension,
or a separately verified sign-aware comparison algorithm. Preserve n-ary EQ and
all cross-bank LT/GT comparisons, constants sized by value, negative constants,
unequal widths and unknown-value policy. Exhaust small signed/unsigned cases
against source semantics before accepting a QoR change. Because `arith.hpp` is
shared with formal cone lowering, an independent reference path is required to
avoid a common implementation bug making synthesis and its checker agree.

## 4. Implementation

### 4.1 Files

| file | content |
|---|---|
| `pass/usyn/xag.hpp/.cpp` | mux kind, fanout refs, `replace`, MFFC, levels, choice classes, compaction |
| `pass/usyn/xag_balance.cpp` | §3.2 (AND / XOR / mux groups, bounded duplication) |
| `pass/usyn/xag_sweep.cpp` | §3.3 |
| `pass/usyn/xag_rewrite.cpp` + `xag_npn4.inc` | §3.4 |
| `tools/xag_npn4_gen/` | offline bounded library generator (not linked into lhd; any dependency decision remains separate) |
| `pass/usyn/function.cpp` | add ISOP and DSD next to the existing truth-table, polarity and signed-cover code (shared with unate analysis) |
| `pass/usyn/multirep.hpp/.cpp` | §3.6 per-cone operator: function, DSD, candidates, conversion back, pricing |
| `pass/usyn/rep_mux.cpp` | bounded mux-BDD candidate (variable order, node limit, abort) and symbolic cofactoring (§3.8) |
| `pass/usyn/rep_sop.cpp` | ISOP, algebraic factoring (reusing `Sp_factorer` / `factor_literals`), SOP balancing |
| `pass/usyn/rep_mig.cpp` | majority recognition, majority-axiom rewrites, expansion to XAG |
| `pass/usyn/xag_choice.cpp` | §3.9 choice classes and technology-aware selection |
| `pass/usyn/residual.cpp` | resub on the mutable graph; P1 and P2 recipe driver (§3.10) |
| `pass/usyn/xag_cost.cpp` | §3.7 Liberty-derived cost table |
| `pass/usyn/xag_lnet.cpp` | mux recognition on import, mux export |
| `pass/usyn/design_synth.cpp` and logical writer integration | P2-R before freeze; P2-C on a private validated CMOS expansion; retain model/provenance (§3.11) |
| `pass/synth/arith.hpp` | balanced `build_eq` |
| `pass/synth/blast.hpp` | semantics-preserving compare audit (§3.12); mux metadata/lowering policy |
| `pass/synth/tmap.hpp`, `pass/abc/abc_tmap.cpp` | optional sharing-hint contract/provider only if M1 experiment succeeds (§3.8) |
| `pass/usyn/native_report.cpp` | per-stage and per-representation counters (§4.3) |

### 4.2 Options and compatibility (`pass.usyn.*`)

Provisional controls: `opt_pre`, `opt_recipe`, `opt_rounds`, `opt_work`,
`balance_dup_limit`, `rewrite_cuts`, `window_inputs`, `sweep_words`,
`reps=xag,mux,sop,mig`, `bdd_node_limit`, `sop_cube_limit`, `choices`, and
`mux_lowering=auto|decode|tree`. Define units, finite defaults, ranges, zero-value
semantics, per-region/global scope and interactions before exposing each option.
Prefer extending existing work/window options where their meanings agree.
A sharing-hint option is deferred to the M1 result; no default fanout barrier.

Start with explicit **legacy versus USYN2** recipe versions. “None” must mean
stage disabled, not an undocumented fallback recipe. Preserve existing
`residual=false` / `feedback=false` ablations; document the full matrix including
P2-C so no hidden optimizer remains enabled in a selection-only experiment.
Do not enable unsupported stages silently. `clock_phases` keeps its existing
meaning and default; physical versus CMOS is not a new unimplemented CLI mode.

All effective choices, bounds, representation order/seed and generated-table
versions enter the native recipe identity; producer code/table changes enter the
USYN salt. Update serialization/report schemas and reject incompatible entries.
Liberty-sensitive keys follow §3.7; sharing hints also enter tmap identity.
Reuse continues under the single `lhd.incremental` switch. Cold/warm equivalence,
name rebinding, interrupted cache writes and replayed work charges must preserve
the existing cache contract. Measure warm reuse on the second run after rebuild.

### 4.3 Reporting

Per stage and region: nodes and levels before/after, wins, zero-gain wins,
**depth rejections and cost rejections** (missing today, needed to explain I5),
duplications, merged equivalences, unconfirmed pairs, choice classes created and
selected, work spent, and limits hit. Per representation: cones attempted,
candidates produced, aborted on a bound (BDD node limit, SOP cube limit, MIG
budget), non-dominated candidates kept, and members finally selected, so the
contribution of each representation is measurable per design. Include validation
failures, care-context rejection, fanout-cycle rejection, stale-evidence
invalidation, scratch/live/choice peak bytes, skipped stages and fallback reason.
Separate proposed, retained, selected and actually mapped wins. Preserve source
bit/state match coverage and classify final LEC/simulation outcomes explicitly.

### 4.4 Determinism and complexity

- Stable source/topological order, normalized literals, deterministic seeds and
  tie-breaks; no pointer or unordered-map iteration determines a result.
- Enumerating cuts merges fanin cut sets before pruning: a retained limit C does
  not make enumeration automatically O(N·C). Sweep buckets can generate O(N²)
  pairs; fanout updates can revisit a large DAG many times; DSD, BDD and SOP can
  be exponential in local support. Bound attempted combinations and mutations,
  not just final retained counts.
- Cap words evaluated, divisor tuples, cover products, BDD recursion/memo entries,
  NPN matches, rewrite visits, choice bytes and all conversion work. A 16-input
  truth table has 1,024 64-bit words; avoid retaining one for every live node.
  Charge allocations before admission and reserve headroom for validation/export.
- All stages/candidates/rounds consume one regional allowance within the existing
  process budget. Give each a finite sub-cap and reserve later-stage credits.
  Report wall time/RSS as well as deterministic credits; cancellation is sticky.
- Near-linear behavior is an empirical scaling gate with fixed local bounds.
  Add adversarial shared-fanout, equal-signature, parity, prime-function and
  wide-selector fixtures. Exhaustion must retain the valid incumbent or clearly
  refuse if final validation/export cannot complete; never publish partial output.

## 5. Milestones and rollout

IDs retain the issue cross-references; table order is the dependency order.
Numeric PPA figures from the original proposal are **targets to investigate**,
not promises or correctness criteria. Enable each stage only after correctness,
resource and measured QoR gates pass; use stage ablations throughout.

| M | content / dependency | gate and diagnostic target |
|---|---|---|
| M0 | freeze reproducible baseline/harness (§1.4); rejection counters; compare audit and balanced EQ experiment (§3.12) | independent small-width semantic checks; reproduce control rows; report both shared-flow baselines; no unproven width removal |
| M1 | isolated decode/flat/tree mapping experiment (§3.8), after M0 | quantify whole-design area/delay and boundary cost; br_mux_bin ≤34 µm² is a hypothesis, not permission to pin every shared node |
| M2 | transactional mux XAG, reference ownership, evidence invalidation, fixed-weight costs and residual port | exhaustive small-graph equivalence; no stale handles/cycles; unchanged legacy behavior; bounded exhaustion/cancellation |
| M3 | balance, after M2 | area/depth and duplication ablations; investigate arb_rr / flow_arb_fixed against same-input abcbare |
| M4 | bounded sweep and partial P1, after M2 | exact confirmations only; measure comparator/icmp and endpoint admission; no requirement to equal unbounded `fraig` |
| M5 | NPN4 library/rewrite, complete P1, after M2–M4 | exhaustive library transforms and zero-gain termination; investigate comparator ≤2.5 µm² |
| M7a | choice ownership/extraction skeleton (§3.9), after M2; Liberty cost/key support (§3.7) | equivalent-class and overlap/cycle tests; preserve incumbent; library-change invalidation; enables retained multi-rep candidates |
| M6 | bounded ISOP/decomposition, SOP/mux candidates and resub, after M5/M7a | per-representation correctness and cost ablations; investigate arb_rr ≤14 µm² and flow_join/priority delay improvements |
| M6b | optional MIG after M6 | verified identities and added benefit on compare/carry/counter cases at fixed total work; defer if no benefit |
| M7 | complete choices and area/depth extraction, after M6 | compare proxy ranking to actual mapping; investigate br_mux_bin(_array) ≤ABC; retain diversity within bounded storage |
| M8 | source mux metadata/tree policy, after M1/M2; shared lowering only after semantic checks | wide/sparse/invalid selector regressions; paired ABC/USYN results; investigate br_demux_bin extra level |
| M9 | P2-R/P2-C integration, options, reports, cache and rollout, after M3–M8 (M6b optional) | full §6 gates; aspirational USYN/ABC area geomean ≤1.0 with delay/runtime/coverage reported |
| M10 | later timing resynthesis, separately scoped | investigate arb_multi_rr/priority delay within 10% of ABC; include area and runtime costs |

Milestone numbering does not postpone cache/schema updates: every stage that
changes an artifact must update its identity and validation when it lands. M9
checks the integrated flow. Include new mux node kinds in every evaluator,
window collector, serializer, report and import/export path before enabling M2.

Before default rollout, keep the legacy recipe available for comparisons and
record the proposed regression tolerances from the frozen baseline. Correctness
and timeout classification are hard gates. QoR acceptance must include per-case
tails, both libraries, peak memory and total synthesis time, not only a favorable
geomean. Do not adjust tolerances after seeing candidate results without an
explicitly documented decision.

## 6. Validation

- **Local correctness:** exhaustive evaluation of generated small graphs before
  and after each operator, deterministic larger-graph simulation fuzz, exhaustive
  NPN4 function/phase/permutation checks (split fixtures if needed for runtime),
  mux degeneracies and priority overlap,
  comparison signedness/width/constant edge cases, reconvergence and shared-root
  deletion. Inject exhaustion at mutation/validation/compaction boundaries;
  check that the incumbent, hashes, references and metadata remain valid.
- **Care and choices:** correlated-leaf examples whose completions differ outside
  the reachable image; forbid global merges. Test complemented equivalences,
  overlapping windows, candidate-on-descendant cycles, choice retention versus
  actual liveness, stale windows after replacement, and deterministic results
  across cold/warm runs and process restarts.
- **State and hierarchy:** both clock phase options, nested module instances,
  sparse register bit names, packed state, complete memory banks, reset/init,
  enables, clock gates, multiple domains, opaque modules and rolled loops.
  Compare semdiff/LEC matches before and after each flow; preserve named state
  boundaries so LEC can reuse cone matches rather than expand entire designs.
- **Controlled ABC comparison:** parameterized external AIG driver with unique
  region dumps, exact input hashes, matching mapping/sizing and constraints.
  Compare native ablations and the §1.2 recipes on identical inputs. Intermediate
  logical graphs and final mapped netlists both need equivalence checks; an AIG
  export with different initialization conventions is not automatically valid.
- **End to end:** external lhdtrack evaluation on ASAP7 and sky130, both source
  frontends, `pass.satopt=false`, all configured cases. Run `lhd lec` against both
  Pyrope and Verilog references, plus independent `lgcheck` against Verilog and
  available simulation legs. No prover inside the optimizer. Require zero refutes
  and simulation mismatches; record unknown/timeout separately and retain the
  existing limits. BMC-only success must include its bound and must never be
  counted as unbounded equivalence. Previously proven cases must remain proven;
  unresolved baseline cases remain visible with a reproduction, not silently
  excluded or called passing. Fix correctness issues before accepting QoR.
- **Results:** publish every attempted synthesis case including failures,
  timeouts, skips and missing netlists. Report ABC/USYN ratio orientation,
  metric-specific positive-pair counts, geometric means, worst regressions and
  every ratio below 0.8; report area, full-design timing/coverage, runtime, peak
  memory, LEC status/bound/runtime and simulation status. Preserve raw evidence
  and tool/source/library hashes for `../lhdtrack/target/report-satsuma.html`.
- **Harness and repository boundary:** adapt the local `real.sh`, `ab.sh` and
  `depth.py` into parameterized developer tools; remove hard-coded repositories,
  tool paths, destructive directory reuse and overwritten dumps. Benchmark
  orchestration stays in lhdtrack or explicitly invoked developer tools.
  LiveHD Bazel tests use only local fixtures/declared dependencies, never sibling
  benchmarks. Contract tests stay immutable. Default tests need no external
  simulator; requested external legs use `LHD_EXTERNAL_SIM` and fail if missing.
- **Build/test gates at implementation time:** `bazel build -c opt //...`,
  appropriate OPT/DBG tests, then the required full suite and ABC-free build/tests.
  Keep each fixture below 20 s OPT / 60 s DBG; large benchmark runs stay external.
  No build or behavioral implementation is part of this planning review.

## 7. Risks and open decisions

- **Mutation scope:** M2 is the largest correctness risk. If bounded transactions
  and evidence rebinding prove too complex, use immutable pass snapshots with
  rebuilding/compaction first; measure whether true in-place replacement is
  needed. Do not trade sound saved-window semantics for speculative speed.
- **Early choice extraction:** native proxies can discard the structure the
  mapper would prefer. Measure this before committing to all four representations
  or a larger library. A separate native covering project may be needed.
- **Mapping hints:** M1 may disprove the barrier hypothesis or show area/delay
  tradeoffs. Keep the current flat contract unless measured benefit justifies
  an explicit extension; no blanket freezing of logical DOMINO inputs.
- **Library generation:** record best-known versus certified optimal structures;
  bound offline search and keep external generator dependencies out of runtime.
- **Representation overhead:** start with XAG plus mux/SOP; MIG is optional.
  Shape/DSD heuristics schedule work rather than proving a representation useless
  (a function can benefit from a BDD without an obvious control variable).
  Bound conversion and retained choices as well as each local search.
- **Objective mismatch:** DOMINO costs, CMOS proxies and mapped PPA can disagree;
  keep separate artifacts, candidate scopes and metrics (§3.11). Balancing can
  duplicate shared logic; full-design timing is the final measurement.
- **Timeout and cache effects:** fair stage reservations must fit existing total
  budgets. Warm reuse must reproduce semantics and charged work. Neither larger
  timeouts nor stale cache hits can serve as evidence of improvement.
- **Unresolved performance:** M0–M9 do not promise ABC-level delay or unbounded
  proof of every arithmetic benchmark. Track remaining cases individually; M10
  and formal-engine improvements are separate work. Unresolved proofs must never
  justify accepting a refute or a simulation mismatch.

## 8. Related work

These works motivate experiments, not expected speedups or implementation reuse.
In particular, mapping-time choice selection and equality saturation are not
algorithms supplied by this plan's bounded candidate/extraction design.

- Hu et al., [Mixed Structural Choice Operator: Enhancing Technology Mapping
  with Heterogeneous Representations](https://arxiv.org/abs/2504.12824), 2025:
  heterogeneous choices with technology-aware evaluation. USYN2 initially
  extracts before the mapper, so it does not retain the same mapping freedom.
- Austin et al., [A Scalable Mixed Synthesis Framework for Heterogeneous
  Networks](https://doi.org/10.23919/DATE48585.2020.9116534), DATE 2020:
  background on mixed-network synthesis; not a specification of USYN2's DSD.
- Chen et al., [E-Syn: E-Graph Rewriting with Technology-Aware Cost Functions
  for Logic Synthesis](https://arxiv.org/abs/2403.14242), 2024, and
  [E-morphic: Scalable Equality Saturation for Structural Exploration in Logic
  Synthesis](https://arxiv.org/abs/2504.11574), 2025: alternative retention and
  extraction. This plan does not add a general equality-saturation engine.
- Mishchenko et al., [Delay Optimization Using SOP
  Balancing](https://people.eecs.berkeley.edu/~alanmi/publications/2011/iccad11_sop.pdf),
  ICCAD 2011: timing-oriented SOP restructuring, with area tradeoffs.
- Yang and Ciesielski, [BDS: A BDD-Based Logic Optimization
  System](https://www.ecs.umass.edu/ece/labs/vlsicad/papers/bds-tcad02.pdf),
  TCAD 2002: BDD-based decomposition; not a guarantee against local BDD blow-up.
- Chatterjee et al., Reducing Structural Bias in Technology Mapping, TCAD 2006;
  the authors' [mapping overview](https://ptolemy.berkeley.edu/projects/embedded/mvsis/techmapping.html)
  explains choice nodes and supergates. A small pre-mapping candidate set is not
  equivalent to a choice-aware covering mapper.

All are inspiration only: no code or library from them is linked into USYN.
