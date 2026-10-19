# pass/domino — a domino gate model for LiveHD synthesis

`pass/domino` holds a model of one domino logic gate and a measurement tool
built on it:

- **`domino_gate`** (library, `domino_gate.{hpp,cpp}`): truth tables of up to
  8 variables, unateness, prime implicants, a minimum-transistor
  series-parallel (SP) factoring of a positive unate function under a stack
  bound, and the decision whether a function fits one domino gate under
  fan-in / stack / transistor limits, in either output rail. It has no graph
  dependency, so it is tested exhaustively on its own
  (`domino_gate_test.cpp`).
- **`usyn_ab`** (binary, `usyn_ab.cpp`): runs pass.usyn's cut-based LUT cover
  on every region of a compiled design and costs each chosen LUT twice, with
  pass.usyn's own transistor count and with the gate model (section 5).

`pass/domino` is not registered as an `lhd` pass, and no code outside this
directory links it.

The gate model follows the model of a domino gate used by the LiveHD synthesis
study *Domino Design Space* (2026-09-20). Section 0 summarises what that study
measured; sections 1 and 2 introduce domino logic and what synthesis for it
has to do; section 3 describes the gate model; section 4 relates it to
`pass/usyn` and `pass/synth`.

---

## Usage

### Build and test

```
bazel build //pass/domino:domino_gate
bazel test  //pass/domino:domino_gate_test
```

Build with `-c dbg` at least once: `assert`s are compiled out under `-c opt`
(`-DNDEBUG`).

### The gate model from C++

The worked example of section 1.5, as `domino_gate_test.cpp` checks it:

```cpp
#include "domino_gate.hpp"
using namespace livehd::domino;

Sp_factorer F;    // keep one per run: cut functions repeat and the memo is the win
Gate_limits lim;  // k = 8 variables, stack = 4 series NMOS, budget = 16 transistors
const int   n = 4;  // q = (a & b) | (c & ~d)
Truth q = truth_or(truth_and(truth_var(0, n), truth_var(1, n)),
                   truth_and(truth_var(2, n), truth_not(truth_var(3, n), n)));

Gate_fit g = F.fit(q, n, lim, Rail::pos);
// g.ok(); g.leaf_rail[3] == Rail::neg; g.formula->to_string() == "ab+cd"
// g.formula->transistors == 4; g.formula->stack == 2

Gate_fit twin = F.fit(q, n, lim, Rail::neg);
// !q = (a'+b')(c'+d): 4 transistors, stack 2
```

- `fit(f, n, limits, rail)` chooses the rail every variable is read in and
  reports the first limit that fails, in the order constant / binate / fanin /
  stack / budget (`Fit_reason`). Formula leaves are variables in their
  positive polarity; `leaf_rail` gives the rail each one is read in.
- `factor(f, n, max_stack, max_transistors)` and `cost(...)` answer the
  minimum-network question for a positive unate, non-constant function
  directly.
- Always pass the real transistor budget: it is what bounds the search
  (section 3).

### usyn_ab

```
bazel build //pass/domino:usyn_ab
usyn_ab lg:DIR top cells.lib [support=6] [literals=16] [series=4]
```

`lg:DIR` is the LiveHD graph library of a compiled design, `top` its top
module and `cells.lib` a Liberty file. The tool runs the pass.abc region
pipeline with a region hook that, like pass.usyn, STRASHes each region and
covers it with pass.usyn's `lut_cover` (under pass.usyn's CLI defaults), costs
every chosen LUT both ways and leaves the region to the ABC flow unchanged.
Scratch output goes to a new temporary directory. It prints the LUTs costed,
the transistor totals of both counts, how many LUTs each count makes cheaper,
histograms by transistor bin and by input count, the admission changes at a
16- and a 12-transistor limit, and examples. Section 5 has measured results.

---

## 0. What the study measured, in one table

The study asks: of every register-input cone (all the logic between register
outputs and one register input), how many can be built with at most *L* domino
gate levels, when each gate is a series-parallel NMOS pull-down over at most
*k* variables, with at most *D* transistors in series, and at most *B*
transistors total. Two rails ("dual rail") means a signal may be available in
both polarities. 48 circuits (EPFL control + arithmetic, ISCAS, OpenCores cut
at registers), 17,101 non-pass-through outputs; every number is a lower bound
(no depth-oriented restructuring, cut pruning can only hide solutions). The 48
benchmark circuits are not part of this repository.

The study's selected design point: dual rail built only on demand, no XOR
gates, B = 16, k = 8, L = 2 (one gate per clock phase, two phases per cycle).

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

Other findings:

- Fitting cones are the *small* ones: **4 to 6 % of the logic** sits in cones
  that fit, even when 36–45 % of the *outputs* fit.
- Twins (a level-1 gate needed in both polarities) are rare: 3–6 % of first
  level gates, about 1.5 rail gates per fitting output. Dual rail costs
  coverage at low D, not area.
- k = 6..8 is the useful fan-in range; k = 10 adds ~0.5 point.
- B = 16 transistors per network is enough at L = 2, even at D = 5, 6.
- XOR support (dual-rail XOR at the cone boundary / end) is worth ~0.2 points
  at D ≥ 4, so the design point has no XOR gates.
- The D = 4 → 5 step is nine pooled points but 91 % of it is two OpenCores
  circuits (`pci_bridge32`, `usb_funct`); the median circuit gains nothing.

The claim "around 50 % of cones fit with stack 4, sane sizes and 2 clocks"
holds for **control logic (52 %)**; pooled over everything it is **36 % at
D = 4 and 45 % at D = 5** (k = 8, B = 16, dual rail on demand).

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
   These are circuit-level concerns; synthesis only has to keep the
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
4. **A netlist that can be emitted, simulated and checked for equivalence**:
   domino gates as black-box cells with a behavioural model equal to their
   Boolean function, two clock phase nets, flops untouched.
5. **A fallback**: cones that do not fit stay static, and a coverage report
   states what fit, what did not and why.

`pass/domino` answers the per-gate question inside step 2: whether a function
is one domino gate under k, D and B, its minimum SP network, and the twin
needed for the opposite rail. It does not build a cover, a phase assignment or
a netlist.

---

## 3. The gate model

- `Truth` (256-bit table, n ≤ 8), cofactor, dual, unateness, prime
  implicants (minimal true points of a positive unate function).
- `Sp_factorer::factor(f, n, stack, budget)`: minimum-transistor series-parallel
  network with series depth ≤ `stack` and ≤ `budget` leaves. Exact table for
  ≤ 4 variables (closure of the literals under AND/OR); for 5..8 variables a
  memoized branch-and-bound over disjoint OR/AND decompositions, kernel and
  kernel-intersection algebraic divisions (SOP and, through the dual, POS),
  and the two Shannon shapes. Measured against a brute force over all 166
  monotone functions of 4 variables under every stack bound: the heuristic
  search alone is already exact there (0 of 664 pairs off).
- Above 4 variables the search is a heuristic: a cheaper network may exist,
  so the model can only understate what fits. Measured example: T4(8) = 55
  transistors against its dual T5(8) = 57.
- `Sp_factorer::fit(f, n, limits, rail)`: rail per variable, then the first
  failing limit in the order constant / binate / fanin / stack / budget. The
  twin (`Rail::neg`) is the dual function over the opposite rails, so an OR8 is
  8 transistors at stack 1 while its twin is an 8-deep stack and fails D = 4.
- **The budget is what bounds the runtime.** Unbounded, the 8-variable
  threshold functions explode (T3(8): 98 s, 478k memo states). With budget 16
  every T_k(8) is decided in < 40 ms; with budget 24 in < 0.7 s. Callers should
  always pass the real budget. For the study's "unbounded" budget, 24 is
  enough in practice (35.89 % at 24 and at infinity).
- Known minima it reproduces: AND2 2, OR2 2, AO21 3, MAJ3 5 (a(b+c)+bc),
  T2(4) 8, T3(4) 8, T2(8) 24, OR8/AND8 8.

---

## 4. Relation to pass/usyn and pass/synth

Nothing in `pass/usyn` or `pass/synth` calls `pass/domino`. The only link is
`usyn_ab`, which reads both without modifying them:

- **The region hook** (`pass/synth/region_backend.hpp`): `Driver_options::region_hook`
  is called once per mapped region with its RAW `Lnet`, before the backend maps
  it, and may return a `Region_rewrite`. `usyn_ab` installs one through
  `Pass_abc::work_with` and always returns "no rewrite", so the ABC flow maps
  every region from its own logic.
- **The cover** (`pass/usyn/lut_cover.hpp`): `lut_cover` is a cut-based cover of
  a STRASH `Lnet` (priority cuts with composed truth tables, a depth-optimal
  round, then area-flow and exact-area rounds that minimise a transistor proxy
  under the `domino_levels` required times). Its `function_cost` admits a LUT
  of at most `support` inputs as a domino gate when, in either polarity,
  `exact_form` (a minimum-literal SOP over primes of at
  most `series` literals) followed by `factor_literals` (a greedy algebraic
  factoring) stays within `literals`. The `Recipe` defaults are support 6,
  literals 16, series 4. In that cost model inputs and outputs are free in
  either polarity.
- **The difference from the gate model.** `Sp_factorer` searches SP networks
  directly (exact table up to 4 variables, branch-and-bound beyond) instead of
  factoring a minimum SOP. Free polarities are the study's "complement free"
  upper reference (45.6 % at D = 4); `fit(..., Rail::neg)` answers the twin
  question of its dual-rail-on-demand model (35.9 %), where an internal
  complement costs a twin gate that must itself fit D and B and only flop
  outputs give both rails free.

---

## 5. Sp_factorer against pass.usyn on real covers

`usyn_ab` costs every LUT that pass.usyn's cover chooses twice: with pass.usyn's
`exact_form(..., factored=true).factored` (the cheaper polarity) and with
`Sp_factorer` (both rails; a binate function is costed as a positive unate
function over the distinct literals of pass.usyn's minimum SOP). Three designs,
literals 16, series 4:

| design | support | LUTs costed | both known | transistors usyn / Sp_factorer | Sp_factorer cheaper | admission changes at 16 |
|---|---|---|---|---|---|---|
| DinoCPU (17 modules, 53 regions) | 6 | 14,511 | 14,341 | 58,411 / 58,387 | 24 | 0 |
| DinoCPU | 8 | 12,966 | 12,677 | 55,469 / 55,430 | 35 | 0 (2 at a 12 limit) |
| BOOM FetchTargetQueue chunk (23 regions) | 6 | 8,862 | 8,801 | 45,604 / 45,604 | 0 | 0 |
| FetchTargetQueue | 8 | 8,555 | 6,799 | 26,802 / 26,802 | 0 | 0 |
| Kogge-Stone 64 | 6 and 8 | 640 | 584 | 1,577 / 1,577 | 0 | 0 |

`Sp_factorer` was never worse than pass.usyn's count, and never "unknown"
(more than pass.usyn's count + 8, the bound `usyn_ab` searches to). The cheaper
cases are all 6- to 8-input functions where the kernel factoring misses a
shared literal, e.g. `(c+d)(ef(a+b)+ab)+cd` for 10 against 11 and
`(f(a+c)+d)(g(a+b)+e)` for 8 against 9.

What the numbers show:

1. **pass.usyn's count is already minimal on 99.8 % of the chosen LUTs** and
   the totals differ by under 0.1 %; no LUT changes admission at a
   16-transistor limit.
2. **The transistor histogram of real gates**: DinoCPU 14,287 gates at 8 or
   fewer, 54 in 9..16, none above 16; FetchTargetQueue (support 6) 7,075 /
   1,726 / 0; Kogge-Stone 583 / 1 / 0. Under k ≤ 8 and D = 4 no chosen gate
   needs more than 16.
3. **Half the cover is binate.** With free dual rail, 7,117 of DinoCPU's 14,511
   costed LUTs and 5,054 of the FetchTargetQueue's 8,862 read some input in
   both polarities (an XOR, a MUX, an enable). Under the study's chosen model
   every such internal read needs a twin gate; this is the gap between its
   35.9 % and 45.6 % columns.
4. **Wide functions.** 170 to 289 DinoCPU LUTs (1 to 2 %) and 1,756
   FetchTargetQueue LUTs at support 8 (20 %) have more than 8 distinct
   literals, beyond the gate model's 256-bit table; `usyn_ab` counts and skips
   them (the "both known" column).

---

## 6. References

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
  LSDL papers (Limited Switch Dynamic Logic), 2004–2005.
