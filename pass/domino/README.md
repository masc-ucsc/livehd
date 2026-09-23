# pass/domino — domino-logic synthesis for LiveHD (design + plan)

Status (2026-09-22): **P0 done, P1 done** (`domino_gate.{hpp,cpp}` +
`domino_gate_test.cpp`, `bazel test //pass/domino/...` green in opt and dbg).
P2 onwards is still plan. Workspace: this clone (`~/projects/livehd-domino`,
upstream `masc-ucsc/livehd` master `b340575cb`), branch `domino`.

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

## 3. Architecture of the pass

```
lhd pass domino --top X lg:DIR --emit-dir lg:OUT --emit report:cov.json
      --set pass.domino.k=8 --set pass.domino.stack=4 --set pass.domino.budget=16
      --set pass.domino.levels=2 --set pass.domino.rail=demand|both|free
      --set pass.domino.xor=false --set pass.domino.cuts=16
      --set pass.domino.mode=report|map

pass/domino/
  domino_gate.{hpp,cpp}     1. gate model: unateness, SP factoring, cost/depth
  domino_cone.{hpp,cpp}     2. bit-blast reg→reg logic to a 1-bit AIG, find cones
  domino_map.{hpp,cpp}      3. cut enumeration + level DP + rail demand + cover
  domino_emit.{hpp,cpp}     4. write domino cell Subs, models, phase clocks back
  domino_report.{hpp,cpp}   5. coverage metrics (the study's columns)
  pass_domino.{hpp,cpp}     6. EPRP registration, options, per-def driver
  *_test.cpp, tests/        gtests + lhd scripts with LEC
```

1. **Gate model** (no graph dependency; exhaustively testable). Input: a
   truth table over n ≤ k variables (256-bit at k = 8). Output: for the
   positive rail and for the negative rail separately, either *infeasible* or
   `{formula tree, transistors, series depth, parallel width}`.
   - Unateness per variable → the required leaf polarity; a binate variable
     makes the cut infeasible (unless XOR mode).
   - Flip negative-unate variables so the function is positive unate; its
     minimal SOP is unique (all prime implicants), so generate primes and
     factor the SOP algebraically into an SP tree (kernel/co-kernel or
     recursive divisor extraction, both exact enough at n ≤ 8; SP-minimal cost
     is NP-hard in general, so this is a heuristic and the coverage stays a
     lower bound, which matches the study).
   - Transistors = leaves of the SP tree; series depth = longest AND path;
     parallel width = widest OR. The **negative rail is the dual tree**
     (swap AND/OR): same transistor count, but depth and width swap, so a
     wide-OR gate has a deep-stack twin and can fail D while its positive
     rail passes. This is why dual rail costs coverage at low D.
   - Cache by the support-compacted positive-unate truth table (P-canonical
     form is a later optimisation).

2. **Cone extraction.** LiveHD nodes are multi-bit (Sum, Mux, Get_mask,
   Hotmux…). The domino model is single-bit, so the register-to-register
   logic of each definition is bit-blasted to an AIG (And + complemented
   edges), reusing what pass.abc already does to feed ABC (see section 5).
   Leaves = flop `Q` bits, graph inputs, constants; roots = flop `din` bits,
   flop enables, graph outputs, memory ports. Pass-through roots (root is a
   leaf) are excluded, as in the study. Large arithmetic is *not* cut out:
   the study maps circuits "as they are".

3. **Mapper.** Standard priority-cut enumeration over the AIG (k ≤ 8, C cuts
   per node, truth tables computed per cut), then a level DP with two rails:

   ```
   level[n][+] = level[n][-] = 0 for every leaf (both flop rails free)
   for n in topological order, for rail r in {+,-}:
     level[n][r] = min over feasible cuts c of n for rail r of
                   1 + max over leaves l of c of level[l][pol(c,l,r)]
   ```
   where `pol(c,l,r)` is the leaf polarity the gate needs. A root fits iff
   `level[root][needed rail] ≤ L`. Ties break on transistors, then on the
   number of internal leaves used in negative polarity (twin demand). The
   cover is then chosen top-down from each fitting root; a twin is materialised
   only when a chosen gate reads a negative internal rail (**demand** mode);
   `both` builds every twin (the study's worst case), `free` treats every
   complement as available at no cost (the study's upper reference; useful
   only as a bound and as a self-check). Cut
   pruning is the same lower-bound caveat as the study; an exhaustive mode for
   k ≤ 6 is the self-check (the study's verifier matched to 0.01 points).

4. **Emission.** Each chosen gate becomes a `Sub` instance of a generated
   black-box cell named by its canonical SP formula (e.g.
   `dom_p4_s2__ab_cd` for `a·b + c·d`), pins `i0..iN-1`, `o`, `clk`. For each
   distinct cell the pass also emits an LGraph behavioural model (And/Or/Not
   over the pins, like `pass liberty gensim`), so `lhd lec` of the mapped
   design against the original is self-contained. Level-1 gates get `clk =
   phi1`, level-2 `clk = phi2` (new top-level clock inputs). Flops are left
   native. Cones that do not fit are left untouched for pass.abc.

5. **Report** (`report:` slot, JSON + a text table): per definition and
   pooled: roots, pass-through roots excluded, roots that fit at 1..4 levels,
   AIG nodes inside fitting cones ("logic in fitting cones"), twins built, rail
   gates per fitting output, transistors per gate histogram, and the reason
   the first non-fitting cut failed (binate / stack / budget / fan-in / depth).
   These are the study's columns, so the pass reproduces the artifact's table.

---

## 4. Algorithm details and decisions

- **Why cut-based and not tree-based mapping.** Zhao/Sapatnekar map trees
  optimally after decomposing into 2-input AND/OR, which loses sharing and
  needs a separate phase-assignment step. Cut enumeration over the AIG with a
  two-rail DP does phase assignment and mapping together and is exactly the
  study's model, so its numbers are reproducible.
- **Why not ABC's `map` with a generated domino library.** A genlib of all
  SP functions to k = 8 / B = 16 is large but feasible; the blocker is that
  ABC's mapper requires and freely inserts inverters and has no notion of
  "negative rail only at leaves or via a twin at the same level" (details in
  5.5). Keep ABC for the static fallback only.
- **Depth vs. area.** Depth (levels) is the hard constraint; area (transistor
  count, twins) is the tie-break and the area-recovery pass runs only among
  covers that keep every fitting root at ≤ L.
- **Constants and buffers.** A gate whose function is a single positive
  literal is a domino buffer (1 transistor): needed when a level-1 signal must
  cross φ2 to reach a register. Count it and expose it in the report; the
  study's coverage does not need it but the real pipeline does.
- **Enables and muxes.** A flop enable is a root of its own cone (as
  `pass.color` already treats it); the mux it implies is positive unate in
  the data inputs and binate in the select, so the select is read as both
  rails from its source. Nothing special: the general machinery handles it.
- **XOR mode** stays as an option (`xor=true`) mirroring the study's control
  experiment: a cut whose function is XOR of two leaves is feasible when both
  leaves offer both rails.
- **Hierarchy.** Per definition, like pass.color / pass.abc; a callee `Sub`
  boundary is a cone boundary. Flatten first (`pass/partition/flatten.cpp`)
  for whole-design numbers.

---

## 5. Hooks into the existing code (surveyed 2026-09-21 on master b340575cb)

The tree is newer than the group's other clones: `lgraph/` is now `graph/`
on top of HHDS (`hhds::Graph`, `hhds::Node_class`, `hhds::Pin_class`), the
`lgshell` REPL and `main/` are gone (2026-06-04), and the only driver is
`./bazel-bin/lhd/lhd`. Sibling libraries (`hhds`, `hlop`, `iassert`) are
fetched by bazel from git pins in `MODULE.bazel:76-148`; the copy already in
`~/.cache/bazel` is an older pin, so build once before trusting its headers.

### 5.1 Pass skeleton: copy `pass/analyze/`
- Registration: `static Pass_plugin plugin("pass_domino", Pass_domino::setup)`
  + `Pass("pass.domino", var)` + `Eprp_method m("pass.domino", help,
  &Pass_domino::work); m.add_label_optional(...); register_pass(m);`
  (`pass/analyze/pass_analyze.cpp:14-45`). The registry is walked at
  `lhd/lhd_kernel.cpp:105`; the BUILD target needs `alwayslink = True`.
- Options: `var.get("k", "8")`, `var.has_label(...)`; input design =
  `var.graphs` (`pass/common/eprp_var.hpp:104-154`). Bool spelling as in
  `pass_analyze.cpp:19`. Diagnostics via `livehd::diag::err/warn/info`
  (`core/diag.hpp`); `Pass::error/warn` are deleted.
- BUILD: engine `cc_library` (`:domino`, no CLI deps) + plugin `cc_library`
  (`:pass_domino`, `alwayslink`) + `cc_test` on `@googletest//:gtest_main`,
  `copts = COPTS` from `tools/copt_default.bzl` (`-Werror`, never `-Wno-*`).
- Wiring into the binary, three edits: add `//pass/domino:pass_domino` to
  `lhd_lib` deps (`lhd/BUILD:138-175`); add
  `{"pass.domino","pass.domino",Set_pass::List::all}` to `kSetPasses`
  (`lhd/lhd_kernel_internal.hpp:200-222`, otherwise `--set pass.domino.*` is
  refused at `lhd_kernel_common.cpp:1030`); add a `domino` arm to
  `pass_command` (`lhd/lhd_kernel_passes.cpp:619+`) and to `kPassSubcommands`
  (`lhd/lhd.hpp:673`). The `single_edge` arm
  (`lhd_kernel_passes.cpp:957-987`) is exactly "load lg:, transform, save
  lg:" and is the one to copy (`load_lg_into_var`, `set_top_label`,
  `merge_sets`, `run_step`, `Hhds_graph_library::save`).
- `lhd synth` integration point: `lhd/lhd_kernel_synth.cpp:218-301` runs
  `pass.color` (alg forced to `synth`) then `pass.abc`; a `synth.domino`
  option goes in `kSynthSetOptions` (`lhd/lhd.hpp:645-666`) and the domino
  step runs between coloring and ABC, marking mapped cones so ABC skips them.

### 5.2 Graph API the pass will use
- Iterate: `for (auto n : g->body().nodes(hhds::Node_order::forward))`.
- Helpers in `graph/node_util.hpp` (`livehd::graph_util`): `type_op_of`
  (:340), `create_typed_node` (:1177), `bits_of` (:392), `find_sink_pin`
  (:936) / `setup_sink_pid` (:1245) / `setup_sink_by_name` (:1262),
  `get_driver_of_sink_name` (:1107), `inp_sink_drivers` (:128),
  `create_const` (:303) / `const_of` (:319, aborts on non-const: probe
  `pin.is_const()` first), `is_type_flop` (:346), `is_type_register` (:352),
  `is_graph_input_pin` (:431), colors `color_of/set_color` (:423-441),
  `ge_weight` (:1862).
- Cell catalog: `graph/cell.hpp:28-138`. Combinational band Sum, Mult, Div,
  Rem, And, Or, Xor, Ror, Not, Get_mask, Set_mask, Sext, Concat, LT, GT, EQ,
  SHL, SRA, LUT, Mux, Hotmux, Clock_cell, Rxor, Popcount; state band Memory,
  Flop, Latch, Fflop, Sub. `Ntype::is_pin_trackable` (`cell.hpp:255`) names
  the pure-wiring ops (Get_mask/Set_mask/SHL/SRA with const amounts, Concat,
  Sext, And/Or masks) that cost zero gates and zero levels. Operands of n-ary
  cells sit in banks by pid parity (even = `as`, odd = `bs`); Mux is `s` at
  pid 0 then `p1, p2, ...`; Hotmux is (control, value) pairs.
- Constants are pins of the builtin `CONST_NODE`, not nodes.
- New per-node attributes (domino level, phase, cell name) go through
  `graph/attrs.hpp` and must be added to `LIVEHD_FOR_EACH_ATTR_TAG`
  (`attrs.hpp:431`) so `graph/attr_carry.hpp` copies them across rebuilds.

### 5.3 Registers: leaves and roots of a cone
`Ntype_op::Flop` sink pids (`graph/cell.cpp:285-307`): 0 `async`, 1
`initial`, 2 `clock_pin`, 3 `din`, 4 `enable`, 5 `negreset`, 6 `posclk`, 7
`reset_pin`, 8/9 `pipe_min/max`; output = driver pid 0 (`Y`). `Latch`
reuses pids 0-7 (its `posclk` is the enable polarity), `Fflop` has `valid`
(0), `din` (3), `stop` (5). `Memory` ports are 16-pid strides (`addr` 0,
`din` 3, `enable` 4). Comptime pins must be probed, never assumed
(`inou/cgen/cgen_verilog.cpp:3463-3525` is the canonical reader).
`graph/latch_contract.hpp` gives `commit_class_of(node, &Design_clocks)`
(:380) for "which clock/edge commits this element", which is what decides
whether a flop is a legal φ2-capture boundary. `pass/color/color_synth_cones.cpp`
already seeds one backward cone per `din` and per `enable` (kPidDin = 3,
kPidEnable = 4) with a CSR fan-in cache; reuse that walk for cone
extraction instead of calling `inp_edges()` per node (quadratic on wide
reset/enable nets).

### 5.4 Region seam shared with pass.abc
`pass/partition/pass_partition.hpp:18-77` (`Region_body`, `Body_builder`)
turns the active coloring into one module per region and is the hook
`pass.abc` uses to replace a region by a mapped netlist. The domino mapper
plugs into the same hook: for a region, try domino; if every root fits emit
domino cells, else hand the region (or the non-fitting cones) to ABC.

### 5.5 Bit-blasting, cell write-back, models, Verilog
- **Region hook.** `Pass_partition::build_decomposition(graphs, outlib, top,
  debug_color, Body_builder hook, Flatten_mode, want_pre_bodies, ...)`
  (`pass/partition/pass_partition.hpp:146`) calls the hook once per colored
  region with a `Region_body` (fresh `body` with IO materialised, read-only
  `src`, `inputs/outputs` ports with `src_driver`, `nodes` span). pass.abc is
  such a hook (`pass/abc/abc_map.cpp:1398 map_regions` → `:1624 map_region`);
  pass.domino is a second one.
- **Bit-blasting without ABC.** `pass/abc/abc_blast.hpp` is templated on an
  opaque `Bit` type plus `Read/Zero/Fail` callbacks: `Wiring_blaster` (:24-120)
  resolves Set_mask/Get_mask/Concat/Sext windows per demanded bit, `blast_comb`
  (:124) lowers And/Or/Xor/Not/Ror, Mux/Hotmux, Sum/LT/GT/EQ, Mult, SHL, SRA,
  Div and constants. `abc_arith_test.cpp` already instantiates the arithmetic
  half with a pure software bit model, so instantiating it with an AIG literal
  as `Bit` is a proven pattern. Adder architecture is selectable
  (`abc_arith.hpp:30 Adder_kind {rca,cska,cla}`), which matters because the
  study maps arithmetic "as is" and a ripple adder is what fits nothing.
- **Cells as black boxes.** A mapped gate is a 1-bit `Ntype_op::Sub` whose
  `GraphIO` declaration lives in the output library: `cell_desc_for`
  (`abc_map.cpp:1359`) find-or-creates the decl (`outlib_->create_io(name)`,
  `add_input(pin, pid)`, `add_output(out, pid)`), `create_typed_node(*body,
  Ntype_op::Sub)` (:4003) instantiates it, `extract_body_bit` (:3881) selects
  input bits with one-hot `Get_mask` and reassembles outputs with `Set_mask`.
  `Ntype_op::LUT` + `attrs::lut` exists but nothing produces it; `Sub` is the
  path opentimer, LEC and cgen are tested against.
- **Behavioural models for LEC.** `pass/liberty/pass_liberty.cpp:109-188
  model_cell`: `create_io(cell)`, `add_input/add_output`, `create_graph()`,
  And/Or/Not nodes from the SOP, `body->commit()`. The domino emitter writes
  the same kind of model per distinct SP formula, so `lhd lec` needs no PDK.
- **Verilog.** `inou/cgen/cgen_verilog.cpp:2887 create_subs` emits every
  `Sub` as a named instantiation; `create_combinational` asserts it never sees
  a `Sub` (:4105). `inou.yosys.fromlg` is just cgen (`inou_yosys_api.cpp:471`).
- **Timing.** `pass/opentimer/opentimer.cpp:713 is_liberty_cell` decides
  whether a `Sub` is timed as a cell; domino cells need a Liberty (phase P6),
  otherwise they hit the `native-comb-boundary` warning.
- **Clock phases.** No clock-domain attribute exists; domains are structural:
  `graph/latch_contract.hpp` `Commit_class`/`Design_clocks`. φ2 is best
  modelled as `Ntype_op::Clock_cell` with `invert` (`graph/cell.cpp:375-396`,
  pids 2 `clk_ref`, 3 `div`, 4 `en`, 6 `invert`) driven by the register clock,
  which is exactly two-phase domino clocking (φ1 gates evaluate on the high
  half, φ2 gates on the low half). `pass/single_edge`'s phase divider
  (`slots`) is the reference for finer schedules but is verification-only.
- **Why not ABC `map` with a generated domino GENLIB.** `Mio_CollectRootsNew`
  requires a buffer and an inverter gate in the library (`abc_map.cpp:534`) and
  the mapper inserts inverters freely, so it cannot honour "no inversion
  inside a cone, negative rails only at registers or via a twin". Keep it as
  an experiment (`flow=abc-genlib`) for comparing area, not as the mapper.
- **History.** `pass/mockturtle` (MIG, cut enumeration, LUT write-back) was
  deleted 2026-05-15 (`9d3c5e3049`) and never built under C++20; there is no
  mockturtle/kitty dependency to lean on. Truth tables to k = 8 are 256 bits,
  so the cut enumerator and canonicalisation are written in-tree (P1/P3).

### 5.6 Build and test conventions
`bazel build -c dbg //pass/domino:all`, `bazel test //pass/domino/...`;
tests < 20 s at `-c opt`; gtests live next to the source (`foo_test.cpp`
only when `foo.cpp` exists, otherwise `*_smoke.cpp`); shell integration
tests as `sh_test(srcs=["tests/x.sh"], data=["//lhd"], tags=["no-sandbox"])`
(`pass/lec/BUILD:106-250`). Graph-building idiom for a unit test:
`pass/analyze/analyze_test.cpp:35-48`; flop fixture:
`pass/abc/satopt_test.cpp:204-213`. End-to-end script shape
(`lhd/tests/latch_clock_identity_test.sh:132-143`):

```
lhd compile design.v --reader yosys-verilog --top top --emit-dir lg:L --workdir w1
lhd pass domino --top top lg:L --emit-dir lg:D --emit report:cov.json --set pass.domino.k=8 --workdir w2
lhd compile lg:D --top top --emit verilog:out.v --workdir w3
lhd lec --impl verilog:out.v --ref verilog:design.v --top top
```

## 6. Phased plan

| phase | deliverable | test / acceptance |
|---|---|---|
| **P0** ✅ | Build the clone, run `//graph/...` and `//pass/analyze/...` tests; note build time and toolchain issues (see "P0 findings" above). | 12/12 green; `//lhd:lhd` is the long LLVM build |
| **P1** ✅ | `domino_gate`: truth-table type, unateness, prime generation, SP factoring under stack and transistor budget, dual/twin, fit decision. | 13 gtests: brute-force exactness for n ≤ 4, random 6..8-variable functions evaluate, 8-variable thresholds under budget in < 5 s |
| **P2** | `domino_cone`: bit-blast per def to an AIG (reuse pass.abc's blasting), leaf/root classification, pass-through exclusion; `mode=report` prints cone statistics. | root/leaf counts on `inou/yosys/tests` fixtures equal a Python oracle over the same Verilog |
| **P3** | `domino_map`: cut enumeration, two-rail level DP, demand/single/both rails, cover selection; `mode=report` emits the study's coverage columns. | on a small in-repo set (a few ISCAS85 + EPFL control circuits < 20 s each) numbers match an independent Python reimplementation; on the 48-circuit set (sibling repo) the pooled 2-level figure at k = 8, D = 4, B = 16, demand rail is **35.9 ± 1.0 %** and the k ≤ 6 exhaustive mode matches within 0.01 |
| **P4** | `domino_emit`: black-box cells + behavioural models + φ1/φ2 clocks; `mode=map` rewrites fitting cones; non-fitting cones untouched. | `lhd lec` PROVEN between original and mapped on every fixture; cgen Verilog compiles in iverilog |
| **P5** | Integration: `lhd pass domino` subcommand, `--set pass.domino.*` in the option registry, `lhd synth --set synth.domino=true` = domino first, pass.abc on the remainder, opentimer on the static part; docs (`pass/domino/README.md`, a `todo/livehd/` task page). | `lhd help`/`lhd list options` show the pass; an end-to-end synth run on the DinoCPU fixture produces a mixed netlist + report |
| **P6** (later) | Domino cell timing: a generated Liberty for the domino cells (delay from stack depth and fan-in, from SPICE in `~/projects/domino/`), so opentimer times the mixed netlist; 3-level variant; domino-aware restructuring for arithmetic. | timing report on mixed netlist; coverage at L = 3 within 2 points of the study's 56 % |

Order of implementation: P1 first because it is graph-independent and
catches the subtle dual-depth rule early; P2/P3 next to reproduce the study
before writing a single graph mutation; P4/P5 last.

---

## 7. Risks and open questions

- **Benchmarks.** The 48 circuits are not in this repo (AGENTS.md: large
  benchmarks live in their own repository). P3 needs a sibling
  `livehd-domino-bench/` with EPFL (github lsils/benchmarks), ISCAS85/89 and
  IWLS-2005 OpenCores, read through `--reader yosys-verilog`. The pooled
  figure is dominated by OpenCores (14,176 of 17,101 outputs), so per-class
  numbers must be checked too.
- **Reproducing a lower bound.** Cut pruning differences move the k = 8
  number by up to 0.3 points in the study; an exact match is not required,
  the k ≤ 6 exhaustive agreement is.
- **What "register" means in the pipeline.** The study keeps explicit
  registers. A real two-phase domino pipeline could use the φ2 gate as the
  storage element; that is a later physical decision and does not change the
  mapping.
- **The hard stop of 36 %.** Only 4–6 % of the logic fits at L = 2. The
  pass is useful as a *hybrid* (domino on small fast control cones, static
  elsewhere) or with L = 3. Make `levels` a first-class knob from day one.
- **Semantics of a partially mapped design.** Domino gates are modelled as
  combinational for LEC/sim; the clocks φ1/φ2 are structural only. State this
  in the report so nobody reads the LEC verdict as a timing proof.

---

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
