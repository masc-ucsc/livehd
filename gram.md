# Pyrope tree-sitter grammar gaps

Valid Pyrope (per `~/projs/docs/docs/pyrope/*.md`) that the current
`tree-sitter-pyrope` grammar **fails to parse** (or parses with the wrong
structure, so it is rejected before lowering). These are *grammar* fixes —
distinct from the lowering/semantic bugs tracked in `todo/pyrope/2f-*`.

Found via adversarial testing; each example below should parse cleanly. Repro:
`./bazel-bin/lhd/lhd compile FILE -q --workdir /tmp/w --set upass.verifier=true`

---

## 1. Attribute slot `[...]` on a lambda declaration

The `[ name = default , ... ]` attribute slot between the lambda name and the
input arg list does not parse. **Attributes are untyped** (`name = value`), so
`[n=1]` is the valid form. Tree-sitter currently reads `g[...]` as a subscript
on a value and chokes on the following `(...)`.

**Should parse** (untyped attributes):
```pyrope
comb g[n=1](x) -> (r) { r = x + n }       // decl: untyped attribute slot
comb addmul[a=2, b=3](x) -> (r) { r = x * a + b }   // comma-separated, with defaults
const v = g[3](x=2)                        // call: override n via the same [...] slot
```

**Current error:** `syntax error: expected 'assign'` (the untyped form is not
parsed at all; `g[n:int=1]` gives `expected 'op_mul'` / `unexpected ...`).

**Grammar should:** the lambda-definition rule must accept an optional attribute
slot — a comma-separated list of **untyped** `name = value` bindings —
positioned **between the lambda name and the input `(...)` arg list** (alongside
the `<...>` generic slot). The call site `foo[N](args)` parses the `[...]` as an
attribute-override slot, not a subscript.

**A typed attribute must be a clean diagnostic, not a raw parse error:**
`comb g[n:int=1](x)` should report *"attributes do not have type declarations"*
(parse the type, then reject it semantically — or reject with that message),
instead of today's generic `syntax error: expected ...`.

**Doc note / conflict:** `06-functions.md` lines 222-227 currently describe this
slot as *typed* comptime parameters (`n:int`, `n:int=1`). That contradicts
"attributes are untyped" — reconcile the doc (attributes = untyped `name=value`).

---

## 2. Range `step` operator

`step` currently sits at the same precedence (`binary_other`) as the range
operators `..=` / `..<` / `..+`, so `a..=b step c` is parsed as a flat
same-precedence chain and rejected as "mixing operators". Parentheses do **not**
help (`(0..=10 step 2)` fails identically).

**Should parse:**
```pyrope
const r = (0..=10 step 2)            // == (0, 2, 4, 6, 8, 10)
for i in 0..<30 step 10 { }          // strides 0,10,20
```

**Current error:** `operators at the same precedence cannot be mixed without
parentheses — the result depends on evaluation order; add parentheses ...`

**Grammar should:** make `step` bind **lower** than the range operators (or fold
`step <expr>` into the range production itself) so `a..=b step c` parses as
`(a..=b) step c` — one range-with-stride node, not three peer `binary_other`
operands. (`2..+3` with no `step` already parses fine.)

**Doc:** `04-variables.md` ~lines 348, 371
(`(0..=10 step 2) == (0,2,4,6,8,10)`, `(0..<30 step 10) == ...`).

---

## 3. Underscore separators in bare decimal literals

Underscores parse inside *prefixed* literals (`0xF_a_0`, `0d1_000`,
`0ub01_1100`) but **not** in a bare decimal literal.

**Should parse:**
```pyrope
cassert(1_000 == 1000)
cassert(1_000_000 == 1000000)
```

**Current error:** `syntax error: unexpected '_000'`
(the lexer stops the decimal literal at the first `_`).

**Grammar should:** allow `_` digit-group separators inside a bare decimal
number literal, same as the prefixed `0d`/`0x`/`0o`/`0ub` forms (underscores
carry no meaning).

**Doc:** `02-basics.md` line 20 (`0xF_a_0 == 4000 // Underscores have no
meaning`); confirmed valid by the language owner.

---

## NOT grammar issues (do not chase these in the grammar)

These parse fine and fail later in lowering/typecheck/fold — they have (or will
get) their own `2f-*` tasks, **not** a grammar change:

- **K/M/G/T scaled literals** (`1K`, `2K+1`, `1G*1024`): parse OK; the comptime
  decoder (`hlop/dlop.cpp`) has no suffix handling (folds wrong / throws). (10
  of the syntax-category findings were actually this.)
- **Parenthesized unary losing its type** (`x or (!y)`, `~(a & b)`,
  `-(-(-5))`, `not true`): parse OK; a typecheck/fold step rejects the operand
  type (`requires boolean/integer operands`, `== requires same type`).
- **Descending ranges** (`b#[1..<-3]`, `1..=-2`): already correctly rejected
  with a clean diagnostic (see `2f-neg_bitrange`); not a parse gap.
- **Removed operators** `~&` / `~|` / `~^` (and `++` / `++=`): correctly *not*
  parsed — they were dropped from the language. (Stale doc lines
  `04-variables.md:555-557` still list `~&`/`~|`/`~^`; remove them.)
