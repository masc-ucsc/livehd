# pass/domino — domino-logic synthesis for LiveHD (design + plan)

Status (2026-09-24): **P0 done, P1 done, plan re-targeted.** Between P1 and
this revision upstream landed `pass/synth` (a backend-neutral region
pipeline) and `pass/usyn` (unate synthesis: a cut-based domino-gate cover of
every region, `lhd synth --set synth.mapper=usyn`). That is most of the P2,
P3 and P5 of the first plan, so those phases are gone and `pass/domino` now
has two jobs: **the gate model** (P1, this directory), to be plugged into
`pass/usyn`'s admission and costing, and **the domino backend** (real
domino cells, two clock phases, dual rail on demand) on `pass/synth`'s
`Region_backend` seam, which upstream's todo calls "the later domino
backend". Sections 3 to 7 describe the re-targeted plan; section 0 to 2 (the
study and the primer) are unchanged. Workspace: `~/projects/livehd-domino`,
branch `domino`, rebased on `origin/master` `5f24eecc5`.

### P0 findings (build and test on the group's machine)
- `bazel 9.2.0`, gcc 15.2, C++23: `//graph/...` and `//pass/analyze/...` build
  in 44 s cold and their 12 tests pass. The domino library and test build in
  seconds; no toolchain fixes were needed.
- **The `lhd` binary pulls LLVM 22 built from source** (`inou/cgen` links the
  JIT for `lhd sim`; `core:file_name` needs `llvm:Support`). A cold
  `bazel build -c opt //lhd:lhd` took about 10 minutes at `--jobs=32` on the
  63-core machine (the LLVM subset, ABC, OpenTimer, slang, cvc5 glue); keep
  the output base. Nothing in P1..P3 needs the binary; P4/P5 do.
- Bazel serialises commands per workspace (one server lock), so a long build
  blocks `bazel test` in the same clone until it finishes.
- Build with `-c dbg` at least once before committing: `assert`s are compiled
  out under `-c opt` (`-DNDEBUG`).
- **2026-09-24: the machine's default gcc moved from 15.2 to 16.2** (a Debian
  package update; `/usr/lib/ccache/gcc` is dated that morning). Bazel's cached
  toolchain was configured against gcc 15's builtin include directories, so
  every compile then failed with "absolute path inclusion(s) found"
  pointing at `/usr/lib/gcc/x86_64-linux-gnu/16/include`. gcc-15 is still
  installed; pin it until the tree is known to build warning-clean under 16:
  `bazel build --repo_env=CC=/usr/bin/gcc-15 --repo_env=CXX=/usr/bin/g++-15 ...`
  (a new toolchain configuration, so the first build after the pin is a
  full rebuild). Nothing in the repo is changed by this; put the two flags
  in `~/.bazelrc` to make them stick.
- Upstream's `//pass/synth:lane_scheduler_test` and `//pass/cost:process_tree`
  were the first targets to hit that error; they are not broken in
  themselves.

### P1 result: the gate model
- `Truth` (256-bit table, n <= 8), cofactor, dual, unateness, prime
  implicants (minimal true points of a positive unate function).
- `Sp_factorer::factor(f, n, stack, budget)`: minimum-transistor series-parallel
  network with series depth <= `stack` and <= `budget` leaves. Exact table for
  <= 4 variables (closure of the literals under AND/OR); for 5..8 variables a
  memoized branch-and-bound over disjoint OR/AND decompositions, kernel and
  kernel-intersection algebraic divisions (SOP and, through the dual, POS),
  and the two Shannon shapes. Measured against a brute force over all 166
  monotone functions of 4 variables under every stack bound: the heuristic
  search alone is already exact there (0 of 664 pairs off).
- `Sp_factorer::fit(f, n, limits, rail)`: rail per variable, then the first
  failing limit in the order constant / binate / fanin / stack / budget. The
  twin (Rail::neg) is the dual function over the opposite rails, so an OR8 is
  8 transistors at stack 1 while its twin is an 8-deep stack and fails D = 4.
- **The budget is what bounds the runtime.** Unbounded, the 8-variable
  threshold functions explode (T3(8): 98 s, 478k memo states). With budget 16
  every T_k(8) is decided in < 40 ms; with budget 24 in < 0.7 s. The mapper
  must always pass the real budget; "unbounded" in the study is 24 in practice
  (35.89 % at 24 and at infinity).
- Known minima it reproduces: AND2 2, OR2 2, AO21 3, MAJ3 5 (a(b+c)+bc),
  T2(4) 8, T3(4) 8, T2(8) 24, OR8/AND8 8.

Source study: *Domino Design Space* (LiveHD synthesis study, 2026-09-20,
claude.ai artifact `Cv17nnUVu52iXfMzudL5rA`). Its model of a domino gate and
its coverage numbers are the acceptance target for this pass (section 6).

---

## 0. What the study measured, in one table

The study asks: of every register-input cone (all the logic between register
outputs and one register input), how many can be built with at most *L* domino
gate levels, when each gate is a series-parallel NMOS pull-down over at most
*k* variables, with at most *D* transistors in series, and at most *B*
transistors total. Two rails ("dual rail") means a signal may be available in
both polarities. 48 circuits (EPFL control + arithmetic, ISCAS, OpenCores cut
at registers), 17,101 non-pass-through outputs; every number is a lower bound
(no depth-oriented restructuring, cut pruning can only hide solutions).

Selected design point: dual rail built only on demand, no XOR gates, B = 16,
k = 8, L = 2 (one gate per clock phase, two phases per cycle).

Pooled over all 48 circuits, k = 8, B = 16, no XOR, share of outputs that fit:

| rail model | D = 3 | **D = 4** | D = 5 | D = 6 | meaning |
|---|---|---|---|---|---|
| complement free (upper reference) | 40.1 | 45.6 | 46.7 | 46.8 | a twin costs nothing |
| **dual rail on demand (the design)** | 25.7 | **35.9** | 45.0 | 46.4 | twin built only when read, must itself fit D/B |
| every gate in both polarities (worst case) | 20.2 | 32.0 | 39.8 | 45.9 | every twin must fit |

Same design point (demand, D = 4), by number of gate levels per cycle:
1 level 19.2 %, **2 levels 35.9 %**, 3 levels 56.4 %, 4 levels 74.3 %.

By benchmark class at the design point (2 levels, demand, D = 4, k = 8):

| class | outputs | fit | logic in fitting cones |
|---|---|---|---|
| EPFL control | 1,525 | **51.7 %** | 5.0 % |
| OpenCores (register-cut) | 14,176 | 36.8 % | 10.4 % |
| ISCAS | 382 | 28.5 % | 3.0 % |
| EPFL arithmetic | 1,018 | 1.8 % | 0.0 % |
| all | 17,101 | 35.9 % | 4.2 % |

Other facts the pass must reproduce or respect:

- Fitting cones are the *small* ones: **4 to 6 % of the logic** sits in cones
  that fit, even when 36–45 % of the *outputs* fit.
- Twins (a level-1 gate needed in both polarities) are rare: 3–6 % of first
  level gates, about 1.5 rail gates per fitting output. Dual rail costs
  coverage at low D, not area.
- k = 6..8 is the useful fan-in range; k = 10 adds ~0.5 point.
- B = 16 transistors per network is enough at L = 2, even at D = 5, 6.
- XOR support (dual-rail XOR at the cone boundary / end) is worth ~0.2 points
  at D ≥ 4: dropped from the design.
- The D = 4 → 5 step is nine pooled points but 91 % of it is two OpenCores
  circuits (`pci_bridge32`, `usb_funct`); the median circuit gains nothing.

The quoted "around 50 % of cones fit with stack 4, sane sizes and 2 clocks"
holds for **control logic (52 %)**; pooled over everything it is **36 % at
D = 4 and 45 % at D = 5** (k = 8, B = 16, dual rail on demand). Treat 35.9 %
pooled / 51.7 % control as the acceptance numbers for D = 4.

---

## 1. Domino logic, from zero

### 1.1 A static CMOS gate, for contrast
A static CMOS gate has two transistor networks that are exact complements: a
PMOS pull-up network to VDD and an NMOS pull-down network to ground. Exactly
one of them conducts for every input pattern, so the output is always driven.
Every input drives one PMOS and one NMOS, PMOS transistors are slow and wide,
and a gate that ANDs many inputs needs a tall PMOS stack. Static CMOS builds
any function, inverting ones included (NAND, NOR, XOR).

### 1.2 A domino gate
A domino gate throws the PMOS network away and replaces it with a single clock
controlled PMOS, the *precharge* transistor:

```
                VDD
                 |
   clk ---o|  precharge PMOS        (on while clk = 0)
                 |
        dyn -----+----------------->|>o----- out      (static inverter)
                 |
        +--------+--------+
        |  NMOS pull-down |   series = AND, parallel = OR
        |  network (a,b,c)|   over the gate's inputs
        +--------+--------+
                 |
   clk ----|  foot NMOS             (on while clk = 1)
                 |
                GND
```

The cycle has two parts:

1. **Precharge** (`clk = 0`): the PMOS pulls the internal *dynamic node* to 1.
   The output inverter therefore drives `out = 0`. The foot transistor is off,
   so nothing can discharge the node.
2. **Evaluate** (`clk = 1`): the PMOS turns off, the foot turns on. If the NMOS
   network conducts for the current inputs, the dynamic node is pulled to 0 and
   `out` rises to 1. If it does not conduct, the node stays at 1 (held by a weak
   *keeper*) and `out` stays 0.

So a domino gate computes `out = f(inputs)` where `f` is exactly "does the NMOS
network conduct": transistors in series implement AND, transistors in parallel
implement OR, and any series-parallel (SP) arrangement is allowed. The output
starts every cycle at 0 and can make **at most one transition, 0 → 1**, during
evaluation. That is the *monotonic* (rising) property.

### 1.3 Why anyone wants this
- **Speed.** Only the NMOS network is in the evaluation path, plus a small
  inverter. Each input drives one small transistor instead of an N and a P,
  so input capacitance roughly halves, and there is no PMOS stack.
- **Wide OR is cheap.** An 8-input OR is 8 parallel NMOS; in static CMOS the
  complementary 8-tall PMOS stack is unusable.
- **Density.** Roughly N + 4 transistors for an N-input function instead of 2N.
- **Cascading.** Because outputs only rise, a gate output feeding the next
  gate's NMOS can only *turn on* transistors, never accidentally discharge a
  node that should stay high. Gates can be chained in one evaluate phase and
  they ripple like falling dominoes, hence the name.

### 1.4 What it costs (these are the synthesis constraints)
1. **Non-inverting.** `f` is "the network conducts", which can only grow when
   an input goes 0 → 1. So `f` must be *positive unate* (monotone increasing)
   in every input. NOT, NAND, NOR, XOR are impossible as single gates, and an
   inverter cannot be inserted between two domino gates (its output would fall
   during evaluation and break monotonicity). Inversions have to be pushed all
   the way back to the registers. A flop can hand out both `Q` and `Q̄` for
   free, so at the cone boundary every literal is available in either polarity.
   Inside the cone, if some reader needs `NOT g` for a domino gate `g`, a second
   gate computing `NOT g` (the *twin* or dual rail) must be built from its own
   inputs' opposite rails. "Dual rail" = carrying both polarities; "on demand"
   = only where some reader needs it.
2. **Stack depth D.** Every series NMOS adds resistance and body effect; the
   discharge gets slower and the noise margin worse. Real designs cap the
   series stack at 3–5. `D = 4` is the study's target machine.
3. **Fan-in k and transistor budget B.** Each parallel branch adds leakage
   and capacitance on the dynamic node (charge sharing, noise). The study
   bounds a gate to k logical variables and B transistors in the network;
   `B = 16` is "no crazy sizes".
4. **Clocking.** Every gate must precharge every cycle and must finish
   evaluating inside its phase. The standard scheme is **two phases per
   cycle**: gates on φ1 evaluate while gates on φ2 precharge, then the roles
   swap. With one gate per phase that is **exactly two gate levels between
   registers**, which is the study's "2 clocks". (Skew-tolerant domino puts
   several gates per overlapping phase; the study's machine does not.)
5. **Power and robustness.** Precharge burns energy every cycle whether or
   not the data changed; dynamic nodes leak and need keepers; the foot can be
   removed (unfooted) only when the inputs are guaranteed low at precharge.
   These are circuit-level concerns; the pass only has to keep the
   structural rules (unate, stack, fan-in, phases).

### 1.5 One worked example
Take a flop-to-flop cone `q = (a & b) | (c & ~d)` with a, b, c, d flop outputs.
As static logic that is an AOI + inverter or two NAND2s + NAND2. As domino:
the function is positive unate in a, b, c and negative unate in d, so feed the
`d̄` rail from the flop and the gate is one NMOS network,
`(a·b) + (c·d̄)`: two series pairs in parallel, 4 transistors, stack 2, fan-in
4. One level, phase φ1; nothing on φ2 (or a domino buffer to keep the pipeline
regular). It fits with room to spare. Now take `q = a ^ b`: binate in both
inputs, so no single domino gate; with both rails it is `a·b̄ + ā·b`, 4
transistors, stack 2, fine at level 1 (inputs from flops) but not deeper in the
cone unless twins exist for its inputs. That is exactly the "boundary XOR" the
study evaluated and dropped.

---

## 2. What "synthesis for domino" has to do

Given a design whose combinational logic between registers is arbitrary
(and/or/xor/mux/adders, produced by the frontend), produce, cone by cone:

1. **A unate (inversion-free) version of the cone**, choosing for every leaf
   which flop rail to read and for every internal signal whether its
   complement (twin) is needed. This is the *output phase assignment* problem
   of Puri/Bjorksten/Rosser (ICCAD 1996): minimise duplication.
2. **A cover of the unate cone by domino gates**, each a positive-unate
   function of ≤ k variables whose minimal SP network has ≤ D series
   transistors and ≤ B transistors, and with at most L = 2 gate levels between
   registers. This is domino technology mapping (Zhao/Sapatnekar ICCAD 1998,
   TODAES 2002), with the study's cut-based formulation.
3. **A phase assignment**: level-1 gates on φ1, level-2 on φ2 (the register
   captures at the end of φ2).
4. **A netlist LiveHD can emit, simulate and LEC**: domino gates as black-box
   cells with a behavioural model equal to their Boolean function, two clock
   phase nets, flops untouched.
5. **A fallback**: cones that do not fit stay static (pass.abc), and a
   coverage report states what fit, what did not and why.

---

## 3. What upstream already provides (as of `5f24eecc5`, 2026-09-24)

`docs/synthesis.html` and `pass/synth/README.md` describe the pipeline; the
pieces this plan builds on:

- **Coloring.** `pass.color synth` has a `usyn` profile (`flop_to_flop=true`,
  no `stop_*` cuts, no control groups, arithmetic inline): every
  combinational path lies inside one color, register to register. Selected
  automatically by `--set synth.mapper=usyn` (`lhd/lhd_kernel_synth.cpp:269`).
- **Region pipeline** (`pass/synth`): private copy, satopt simplification,
  arithmetic/memory modules, cut into regions (`pass/partition`),
  translation of a region into a RAW `Lnet` (`region_blast.cpp`, the
  operator lowering of `blast.hpp` and the adders of `arith.hpp`), region
  cache, parallel lanes, read-back of a `Cell_netlist` (`region_writer.cpp`).
- **The Lnet** (`lnet.hpp`): a flat k-LUT network, node 0 the constant, sources
  (inputs and latch outputs), LUTs of up to 8 fanins with a truth table (no
  complemented edges). `strash()` gives the hashed 2-input form the cover
  reads; latches are the crossing registers, so register-to-register cones
  are explicit.
- **The backend seam** (`region_backend.hpp`): `Region_backend::map(ctx,
  blast, rewrite, qor) -> Cell_netlist`; ABC is the only backend today
  (`pass/abc/abc_backend.cpp`). `Driver_options::region_hook` is called with
  the RAW Lnet before the backend and may return a `Region_rewrite` (logic
  over the same boundary, for the flow or for technology mapping only).
- **Unate synthesis** (`pass/usyn`): the region hook that covers a STRASH Lnet
  with domino gates. `lut_cover.cpp`: priority cuts (`cover_cuts` = 12),
  composed truth tables to `support` inputs (default 6), every cut function
  costed once by `function_cost`; a depth-optimal round with
  `domino_levels` = 2 required times, then area-flow and exact-area
  recovery of a transistor proxy. `unate.cpp`: `exact_form` = exact
  minimum-literal SOP over primes of at most `series` literals (default 4),
  then `factor_literals` (a recursive kernel factoring) for the pull-down
  transistor count, checked against `literals` (default 16). Both polarities
  are tried and the cheaper kept: **inverters are free** ("every signal is
  available in both polarities"). The cover is handed to ABC as SOP LUTs and
  mapped onto the static Liberty cells; the report (`<qor>.usyn.json`) has
  domino gates by input count and series depth, outputs by domino depth,
  cones by gate count and the transistor proxy totals.

What it does not have, and this plan adds:

1. A minimum-transistor factoring with an exactness argument (P1 here).
   `factor_literals` is a heuristic without the exact table, kernel
   intersections or branch-and-bound; its count decides admission at the
   16-literal limit.
2. The study's rail model. Free complements are the study's "complement
   free" upper reference (45.6 % at D = 4); its chosen design is dual rail
   on demand (35.9 %), where an internal complement costs a twin gate that
   must itself fit D and B, and only flop outputs give both rails free.
3. The study's parameters as a profile: k = 8 (upstream defaults to 6),
   D = 4, B = 16, two levels, and the coverage columns the artifact reports.
4. A domino backend: real domino cells with φ1/φ2, behavioural models for
   LEC, a Liberty for timing. Today the cover ends as static cells.

## 4. Re-targeted architecture

```
pass/domino/
  domino_gate.{hpp,cpp}   P1  gate model (done): min-transistor SP factoring, rails, fit
  usyn_ab.cpp             P2  A/B tool (done): pass.usyn's count vs the gate model on real covers
  domino_rails.{hpp,cpp}  P3  rail-demand accounting over a cover: twins, dual stack, study columns
  domino_backend.{hpp,cpp} P4 Region_backend: domino cells + phase clocks + models
  pass_domino.{hpp,cpp}   P5  `lhd pass domino` / `synth.mapper=domino`
```

- **Gate model into usyn (P2/P3).** `function_cost` (`lut_cover.cpp:174`)
  calls `exact_form(t, literals, series, factored=true)` per polarity and
  admits a domino gate when `form.factored <= literals`. The plug-in point is
  that one call: replace the `factored` count by `Sp_factorer::fit(t, n,
  {k, series, literals}, rail)` (keeping their exact SOP as the side data
  ABC receives). Admission then uses the minimum network, and the SP
  formula itself is available for the backend. The A/B tool measures the
  effect before the swap.
- **Rail demand (P3).** Over a finished cover, walk the LUTs: a gate reading a
  leaf in negative polarity demands the leaf's negative rail; a flop leaf or
  a region input gives it free; an internal leaf needs a twin gate =
  `fit(leaf function, Rail::neg)`, which may fail on D (the dual-stack rule)
  or B. Report the study's columns per region and pooled: outputs by domino
  depth with and without twins, twins per fitting output, logic inside
  fitting cones. Then feed it back into the cover's cost (a negative internal
  literal costs the twin's transistors) so the cover minimises the real
  dual-rail cost. This is where the 35.9 % versus 45.6 % gap lives.
- **Domino backend (P4).** A `Region_backend` whose `map` takes the cover
  network (each LUT with its SP formula) and returns a `Cell_netlist` of
  black-box domino cells: one cell per distinct SP network, pins `i0..`, `o`,
  `clk`; level-1 gates on the design clock (φ1), level-2 gates on its
  inverse through a `Clock_cell` with `invert` (φ2); twins as separate
  cells; static LUTs and non-fitting cones handed to the ABC backend as
  today (a mixed netlist). Behavioural models per cell as `pass liberty
  gensim` does, so `lhd lec` proves the mixed netlist; a generated Liberty
  (delay from stack and fan-in, from the SPICE runs in `~/projects/domino/`)
  so `pass.opentimer` times it.
- **CLI (P5).** `synth.mapper=domino` = the usyn cover with the gate model
  and rail demand, mapped by the domino backend; `pass.domino.*` settings
  for k, D, B, levels, rail mode (`demand|both|free`) and the report.

## 5. Hooks into the current code

- `pass/usyn/lut_cover.cpp:174 function_cost` and
  `pass/usyn/unate.hpp` (`exact_form`, `Form::factored`, `Recipe`,
  `Cover_cost`, `Search_options`): admission and costing.
- `pass/usyn/lut_cover.hpp`: `Cover_lut {root, leaves, table, fn, level}`,
  `Cover_result` (counts, `source_literals_pos/neg`, `cone_gates`),
  `cover_network` (the coarse Lnet handed to the backend).
- `pass/usyn/usyn_region.cpp rewrite_region`: strash, cover, hand-off,
  report row; `pass/usyn/pass_usyn.cpp:195-235`: how the hook is installed
  through `Pass_abc::work_with(var, configure)`.
- `pass/synth/region_backend.hpp`: `Region_backend`, `Region_rewrite`,
  `Region_hook`, `Driver_options`; `pass/synth/cell_netlist.hpp`: what a
  backend returns; `pass/synth/region_writer.cpp`: how cells become `Sub`
  nodes in the region body.
- `pass/synth/lnet.hpp`: `Lnet`, `strash`, `combinational_outputs`.
- `pass/abc/abc_backend.cpp`: the reference backend; `pass/abc/abc_lnet.cpp
  lnet_into_logic`: how an SOP Lnet becomes ABC logic.
- `pass/liberty/pass_liberty.cpp:109-188 model_cell`: behavioural model
  per cell for LEC. `graph/cell.cpp:375-396 Clock_cell` (`clk_ref`, `div`,
  `en`, `invert`): φ2.
- `lhd/lhd_kernel_synth.cpp:135 find_mapper`, `lhd/lhd.hpp` (`kSynthSetOptions`,
  the mapper table): registering `domino` as a mapper.
- Pass skeleton and wiring as in P1 (`pass/analyze`, `lhd/BUILD`,
  `kSetPasses`, `pass_command`); `pass/usyn/pass_usyn_test.cpp` shows how to
  drive a mapper from a test (`Eprp_var` dict, `Scoped_instance`).
- Build and test conventions unchanged (section "P0 findings").

### P2 result: the A/B on real covers (2026-09-24)

`bazel build //pass/domino:usyn_ab`, then `usyn_ab lg:DIR top cells.lib
support literals series`. It runs pass.usyn's own cover (STRASH + `lut_cover`,
its CLI defaults) on every region and costs each chosen LUT twice: upstream's
`exact_form(..., factored=true).factored` (the cheaper polarity) and
`Sp_factorer` (both rails; a binate function over the distinct literals of
upstream's minimum SOP). Three designs, literals 16, series 4:

| design | support | LUTs costed | both known | transistors theirs / mine | mine cheaper | flips at 16 |
|---|---|---|---|---|---|---|
| DinoCPU (17 modules, 53 regions) | 6 | 14,511 | 14,341 | 58,411 / 58,387 | 24 | 0 |
| DinoCPU | 8 | 12,966 | 12,677 | 55,469 / 55,430 | 35 | 0 (2 at a 12 limit) |
| BOOM FetchTargetQueue chunk (23 regions) | 6 | 8,862 | 8,801 | 45,604 / 45,604 | 0 | 0 |
| FetchTargetQueue | 8 | 8,555 | 6,799 | 26,802 / 26,802 | 0 | 0 |
| Kogge-Stone 64 | 6 and 8 | 640 | 584 | 1,577 / 1,577 | 0 | 0 |

Mine was never worse (and never "unknown", i.e. never more than theirs + 8).
The cheaper cases are all 6- to 8-input functions where the kernel factoring
misses a shared literal, e.g. `(c+d)(ef(a+b)+ab)+cd` for 10 against 11 and
`(f(a+c)+d)(g(a+b)+e)` for 8 against 9.

What this decides:

1. **No factorer swap.** Upstream's count is already minimal on 99.8 % of the
   chosen LUTs and the total differs by under 0.1 %; nothing changes
   admission at 16. The gate model's value is elsewhere: the SP formula the
   backend will instantiate, the twin/dual-stack rule, and rail demand.
2. **The transistor histogram of real gates** (answering "how many are 8,
   16, 24 and more"): DinoCPU 14,287 gates at 8 or fewer, 54 in 9..16, none
   above 16; FetchTargetQueue (support 6) 7,075 / 1,726 / 0; Kogge-Stone
   583 / 1 / 0. Under k ≤ 8 and D = 4 no chosen gate needs more than 16.
3. **Half the cover is binate.** With upstream's free dual rail, 7,117 of
   DinoCPU's 14,511 costed LUTs and 5,054 of the FetchTargetQueue's 8,862
   read some input in both polarities (an XOR, a MUX, an enable). Under the
   study's chosen model every such internal read needs a twin gate. This is
   the 35.9 % versus 45.6 % gap, and P3's job.
4. **A gate-model gap.** 170 to 289 DinoCPU LUTs (1 to 2 %) and 1,756
   FetchTargetQueue LUTs at support 8 (20 %) have more than 8 distinct
   literals, beyond the 256-bit table. The factorer needs a cube-set
   (literal) path for those before it can cost every gate: added to P3.

## 6. Phased plan (re-targeted)

| phase | deliverable | acceptance |
|---|---|---|
| **P0** ✅ | Build and test; findings above. | green |
| **P1** ✅ | Gate model (`domino_gate`). | 13 gtests, exact for n ≤ 4 |
| **P2** ✅ | `usyn_ab`: pass.usyn's cover on real designs, every chosen LUT costed both ways. Outcome: no factorer swap (see "P2 result"). | numbers above |
| **P3** | Gate model: a cube-set path for functions with more than 8 literals, the dual trick, the 5-input exact table. Rail demand over a cover: twins, dual-stack, the study's columns in the usyn report; then the twin cost in the cover. | on the 48-circuit set, pooled 2-level coverage 35.9 ± 1 % (demand) and 45.6 % (free) at k = 8, D = 4, B = 16 |
| **P4** | Domino backend: cells, φ1/φ2, models, mixed netlist with ABC. | `lhd lec` PROVEN on every synthesis fixture; iverilog compiles the Verilog |
| **P5** | `synth.mapper=domino`, `pass.domino.*`, docs, todo page. | end-to-end on DinoCPU |
| **P6** | Liberty for domino cells and opentimer timing; 3-level option; domino-aware restructuring. | timing report on the mixed netlist |

## 7. Risks and open questions

- **Benchmarks.** The 48 circuits are still not in the repo; P3's acceptance
  needs a sibling benchmark directory (EPFL, ISCAS, IWLS OpenCores) read
  through `--reader yosys-verilog`.
- **Exactness above 4 inputs.** The gate model can overestimate; measured
  evidence: T4(8) = 55 against its dual T5(8) = 57. Cheap fixes in order:
  take the minimum with the dual formula (series/parallel swapped), extend
  the exact table to 5 inputs, lift the divisor caps for rejections within
  two transistors of the budget.
- **Upstream is moving.** pass/usyn landed in two days and its todo page
  (`synth-unate`) is linked from the index but not in the tree. Rebase often;
  keep every change to pass/usyn behind a setting and measured by the A/B.
- **Only 4 to 6 % of the logic fits at two levels.** The value is a hybrid
  netlist and the knob `domino_levels`; the backend must coexist with ABC's
  static cells from day one.
- **LEC is not a timing proof.** Domino cells are modelled as combinational;
  φ1/φ2 are structural. Say so in every report.

## 8. References

- Puri, Bjorksten, Rosser, *Logic optimization by output phase assignment in
  dynamic logic synthesis*, ICCAD 1996. (Phase assignment to minimise dual-rail
  duplication.)
- Zhao, Sapatnekar, *Technology mapping for domino logic*, ICCAD 1998; and
  *Technology mapping algorithms for domino logic*, ACM TODAES 2002. (Tree
  mapping to a parameterised library with series/parallel limits.)
- Thorp, Yee, Sechen, *Domino logic synthesis using complex static gates*,
  ICCAD 1998; *Monotonic static CMOS and dual-Vt technology*, ISLPED 1999.
- Harris, *Skew-Tolerant Circuit Design*, Stanford PhD 1999 / Morgan Kaufmann
  2001. (Two-phase domino pipelines, footed/unfooted gates, keepers.)
- Weste & Harris, *CMOS VLSI Design*, ch. 9 (dynamic and domino circuits).
- Montoye et al., *A double precision floating point multiply*, and the IBM
  LSDL papers (Limited Switch Dynamic Logic), 2004–2005, for the flavour the
  group has taped out.
