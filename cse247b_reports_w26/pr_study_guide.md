# PR Study Guide — CSE 247B Spring 2026
> All merged PRs to `masc-ucsc/livehd` as of May 2026.
> Read top to bottom — each section builds on the last.

---

## Background: What is this project?

LiveHD is a hardware compiler. It takes Pyrope source code (`.prp` files),
converts it into an internal tree called LNAST (Language-Neutral AST), runs
optimization passes (uPass), and eventually produces hardware circuits.

Your job this semester was to build and improve the **uPass** optimization
engine — the part that simplifies and optimizes LNAST before it becomes
hardware.

---

## PR #516 — Bug Fixes (P1)

**What:** 7 bug fixes across uPass.

**Why it mattered:** Before any new features could land, the existing code had
broken behavior. These fixes stabilized the foundation everything else is
built on.

**Key idea:** Fix first, build second.

---

## PR #531 — Constprop, DCE, and Verifier (Slice 1 + 2 + P2)

This is the biggest PR. Three things landed at once.

### Slice 1: Constant Propagation + Dead Code Elimination

**What is constprop?**
If the code says `mut x = 2 + 3`, there's no reason to compute that at
runtime — it's always 5. Constprop folds it to `mut x = 5` at compile time.

**What is DCE (Dead Code Elimination)?**
If `x` is never used after being assigned, there's no reason to keep it.
DCE removes it.

**How it works in LNAST:**
Each LNAST node (like `plus`, `assign`, `eq`) has a result variable.
Constprop evaluates constant expressions and replaces result variables with
their known values. DCE then removes any node whose result is never read.

### Slice 2: Verifier resolves `cassert` at compile time

**What is `cassert`?**
It's a compile-time assertion — like `cassert x == 5` meaning "x must equal
5 or this is a bug."

**What did Slice 2 do?**
After constprop knows `x = 5`, it can evaluate `cassert x == 5` right now
(it's true — pass) instead of leaving it for runtime. This is the verifier
pass.

### P2: Tuple operations in constprop

Tuples in Pyrope are like structs: `(a, b, c)`. P2 extended constprop to
handle:
- `tuple_add` — building a tuple
- `tuple_get` — reading a field
- `tuple_set` — writing a field
- N-ary folding — folding operations with more than 2 operands
- `sext` — sign extension

---

## PR #532 — If-Branch Pruning (Slice 7)

**The problem:**
```pyrope
mut c = 1
if false {
  c = 2
}
```
The `if false` block can never execute. But the compiler was keeping it
anyway, wasting work.

**What Slice 7 does:**
It looks at the condition of every `if` statement. If constprop already knows
the condition is `true` or `false`, it:
- Prunes the dead branch entirely
- Splices the live branch's statements directly into the parent

**How:** In `process_if()` inside the uPass runner, after constprop runs,
the condition is checked. If it resolves to a constant, the dead branch is
removed from the LNAST tree.

**Known limitation documented:**
In elif chains, the symbol table gets pre-populated from the dead branch as a
side-effect of how `dispatch_to_passes` works. This is noted in a comment —
it's a known quirk, not a bug you introduced.

---

## PR #538 — `pass.prp_writer` lgshell Pass

**Context: what is lgshell?**
LiveHD has a built-in shell called lgshell. You can chain compiler passes
together with `|>`:
```
inou.prp files:foo.prp |> pass.upass |> pass.prp_writer odir:out/
```
This reads a `.prp` file, runs uPass on it, then writes the result back out
as Pyrope source.

**What `Lnast_prp_writer` already did (before this PR):**
There was already a class that could walk an LNAST and emit Pyrope text.
But it was standalone — you couldn't invoke it from lgshell.

**What this PR added:**
Wired `Lnast_prp_writer` as a proper lgshell pass called `pass.prp_writer`.

Files added:
| File | What it does |
|------|-------------|
| `pass/prp_writer/pass_prp_writer.hpp` | Declares the pass class |
| `pass/prp_writer/pass_prp_writer.cpp` | Implements `setup()` and `work()` |
| `pass/prp_writer/BUILD` | Bazel build target |
| `pass/prp_writer/tests/prp_writer_roundtrip_test.sh` | Round-trip test |

**How a pass works in LiveHD:**
1. Register with `Pass_plugin` (static, fires at startup)
2. `setup()` — declares the pass name and any options (like `odir`)
3. `work(Eprp_var& var)` — iterates over `var.lnasts`, writes each one

**`attr_set x type mut` suppression:**
Prof. Renau showed a `lnast.dump` where every `mut x = ...` in Pyrope
creates an `attr_set x type mut` node in the LNAST. These are internal
bookkeeping — they should never appear in the output. This PR suppressed them
so they emit nothing.

**Round-trip test:**
The test runs `trivial_if.prp` through the full pipeline and re-parses the
output to confirm it's valid Pyrope.

---

## PR #539 — Correctness Fixes in `Lnast_prp_writer`

Three bugs fixed.

### Fix 1: Spurious `comb NAME() {}` wrapper

**The bug:**
`write_top()` always wrapped everything in `comb NAME() {}`:
```pyrope
comb pp() {      ← WRONG — pp.prp is a bare file, not a function
  mut c = 1
  ...
}
```

**Why it was wrong:**
Bare Pyrope files (like `pp.prp`) are just plain statements — no enclosing
function. The `comb` wrapper is only for files that explicitly declare a
`comb` function. That case is handled by `write_func_def()` (Slice 4, not
yet implemented).

**Fix:**
`write_top()` now just emits the children directly — no wrapper.

---

### Fix 2: `mut` keyword on reassignments

**The bug:**
```pyrope
mut c = 1      ← correct, first assignment
if false {
  mut c = 2    ← WRONG — this is a reassignment, not a new declaration
}
```

**Why it happened:**
Every write function (assign, infix ops, etc.) was doing:
```cpp
if (!is_tmp(lhs)) { print("mut "); }
```
— meaning: "if it's not a temp variable, always print `mut`." But `mut`
should only appear at the *declaration site*.

**The fix — `pending_decl_` map:**
The `attr_set x type mut` node is what marks the declaration. So now:

1. `write_attr_set()` sees `attr_set c type mut` → records `"c" → "mut"` in
   a map called `pending_decl_`, emits nothing.
2. The *next* assignment to `c` calls `take_decl_keyword("c")` → gets `"mut"`,
   removes it from the map, emits `mut c = ...`.
3. Any later assignment to `c` calls `take_decl_keyword("c")` → finds nothing,
   emits `c = ...` (no `mut`). ✓

---

### Fix 3: Stale `pending_decl_` entries in `tuple_set` and `delay_assign`

**The bug:**
`write_tuple_set()` and `write_delay_assign()` both write to a variable but
never called `take_decl_keyword()`. So if a `mut` was pending for variable
`x` and the next thing was a tuple set to `x`, the `mut` entry would sit in
the map. Then some *later*, completely unrelated assignment to `x` would
accidentally pick it up and emit `mut x = ...` when it shouldn't.

**Fix:**
Both functions now call `take_decl_keyword(lhs)` immediately when they read
the LHS variable. They discard the result (neither `tuple_set` nor
`delay_assign` uses the `mut` keyword in Pyrope), but the map entry is cleared so it can't leak.

---

## Quick Reference: Key Concepts

| Term | What it means |
|------|--------------|
| LNAST | The internal tree representation of Pyrope code |
| uPass | The optimization engine that transforms LNAST |
| constprop | Evaluating constant expressions at compile time |
| DCE | Removing code whose result is never used |
| cassert | Compile-time assertion — verified by the verifier pass |
| lgshell | LiveHD's command-line shell for chaining passes |
| `Eprp_var` | The pipeline object passed between lgshell passes; holds `.lnasts` |
| `attr_set x type mut` | LNAST-internal marker for "x is declared mutable here" |
| `pending_decl_` | Map tracking which variables have a pending `mut`/`reg`/`wire` keyword |
| `take_decl_keyword()` | Reads AND removes an entry from `pending_decl_` (consume-once) |
