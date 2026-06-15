# `lhd lec` BMC segfault — abc↔cvc5 CaDiCaL symbol clash (NOT a cvc5 algorithm bug)

## TL;DR

`lhd lec ... --set lec.engine=bmc` intermittently **segfaults inside cvc5's
bundled CaDiCaL** (`CaDiCaL::Internal::subsume_round`) during `checkSat()`.

It is **not a cvc5 solver bug**: the exact failing SMT query, dumped to a
standalone `.smt2`, **solves cleanly (`unsat`) on the standalone `cvc5` binary**.
It only crashes inside the `lhd` process. The cause is a **link-time symbol
clash**: `lhd` statically links **both** Berkeley-abc and cvc5, and **both vendor
their own CaDiCaL**. CaDiCaL's `Reap` class (and `ipasir_*`/`ccadical_*` C API)
live in the GLOBAL namespace, so the two copies collide; cvc5's CaDiCaL ends up
calling abc's incompatible `Reap` (different layout) and corrupts memory.

So the fix is a **livehd build/linking fix** (finish isolating cvc5's CaDiCaL),
or an abc/cvc5 build-namespacing change — **not a patch to cvc5's algorithms.**

## Evidence

- `engine=ind` (single `checkSat`) does NOT crash — its smaller queries don't
  reach CaDiCaL's subsume/elim inprocessing.
- `engine=bmc` at depth ≥ ~13 on a small cross-reader counter crashes
  deterministically. `ulx3s_uart_tx` proves fine at depth 20 — instance-specific
  (whether the SAT run triggers subsume/elim).
- The dumped `.smt2` (≈75 lines, QF_BV) → `bin/cvc5 crash.smt2` → `unsat`, exit 0.
  Same query in-process → SIGSEGV.
- A bigger process stack (`ulimit -s 512MB`) does NOT help → memory corruption,
  not stack overflow.

### Stack trace (cvc5 1.3.4, Linux x86_64)
```
CaDiCaL::Internal::subsume_round()
CaDiCaL::Internal::subsume(bool)
CaDiCaL::Internal::elim(bool)
CaDiCaL::Internal::cdcl_loop_with_inprocessing()
CaDiCaL::Internal::solve(bool)
...
cvc5::internal::theory::bv::BVSolverBitblast::postCheck(...)
...
cvc5::Solver::checkSat()
livehd::lec::prove_equal(...)            # pass/lec/query.cpp (BMC engine)
```

### The clash, in the linked binary
```
$ nm bazel-bin/lhd/lhd | grep _ZN4Reap4initEv
  ...74822b0 t _ZN4Reap4initEv     # cvc5's, localized (partial fix below)
  ...5f580c0 T _ZN4Reap4initEv     # abc's, still GLOBAL  <-- collides
```
`lhd` ends up with BOTH a local (cvc5) and a global (abc) `Reap`; references that
were not bound to cvc5's local copy resolve to abc's global one.

## Partial fix already in tree (`packages/cvc5.BUILD`)

The Linux CaDiCaL-localize step swept only `lib/libcadical.a`, but in cvc5 1.3.4
the `Reap` class and the `ipasir_`/`ccadical` C API are compiled into a SEPARATE
object, so they stayed global. The BUILD now also localizes those by name from
the combined object (mirroring the macOS `-unexported_symbol` patterns):
```
nm --defined-only --extern-only "$work/combined.o" \
  | awk '$2 ~ /^[TDBRVW]$/ {print $3}' \
  | grep -E '^(ipasir_|ccadical|_ZN4Reap)' >> "$work/cad.syms"
```
This made cvc5's `Reap`/`ipasir_` LOCAL — but the crash persists, so **at least
one more CaDiCaL global still clashes** (subsume/elim touches more than `Reap`).

## Recommended fix for the separate task

Pick one:

1. **Localize ALL of cvc5's bundled-CaDiCaL globals** (robust). In
   `packages/cvc5.BUILD`, instead of name patterns, localize every global in
   `combined.o` that is NOT part of cvc5's public API — e.g. every defined global
   that also appears as a global in abc's `libabc.a` (the intersection is exactly
   the clash set), or every non-`cvc5`/non-`Cvc5`-prefixed defined global.
2. **Namespace one CaDiCaL** at build time (objcopy `--redefine-syms` to prefix
   cvc5's CaDiCaL symbols, or abc's), so the two never share a name.
3. **Isolate abc's SAT** from the cvc5 link if abc's CaDiCaL is unused by the
   linked passes.

Verify: `nm bazel-bin/lhd/lhd | grep -E ' T (_ZN4Reap|ipasir_|ccadical)'` should
be empty (only cvc5-private, file-local copies remain), and the repro below must
return `Proven`/`Refuted`, never SIGSEGV.

## Reproduce

```sh
# 1. a tiny flop design
cat > /tmp/ctr.sv <<'EOF'
module ctr(input logic clk_i, input logic rst_ni, input logic en_i, output logic [7:0] q_o);
  logic [7:0] cnt_q;
  always_ff @(posedge clk_i or negedge rst_ni)
    if (!rst_ni) cnt_q <= 8'h0; else if (en_i) cnt_q <= cnt_q + 8'h1;
  assign q_o = cnt_q;
endmodule
EOF
LHD=bazel-bin/lhd/lhd
# 2. emit it through BOTH readers to lg: graphs
$LHD compile /tmp/ctr.sv --reader slang       --top ctr --emit-dir lg:/tmp/ci --workdir /tmp/wi -- --allow-use-before-declare
$LHD compile /tmp/ctr.sv --reader yosys-slang --top ctr --emit-dir lg:/tmp/cr --workdir /tmp/wr -- --allow-use-before-declare
# 3. cross-reader BMC at depth 13 -> SIGSEGV in CaDiCaL::subsume_round
$LHD lec --impl lg:/tmp/ci --ref lg:/tmp/cr --top ctr --set lec.engine=bmc --set lec.bound=13
# (depth <= 12 proves fine; ind engine proves fine)
```

## Workaround until fixed

`engine=ind` (the default) is unaffected. For designs where `ind` false-REFUTEs
(unreachable states) and `bmc` is needed, lower `lec.bound`, or use `lhd check`
(yosys lgcheck) which doesn't link the cvc5 CaDiCaL.
