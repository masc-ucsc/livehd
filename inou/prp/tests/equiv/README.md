# `inou/prp/tests/equiv` — equivalence fixtures

Two axes live here, and a design can sit on both:

| axis | files | target |
| --- | --- | --- |
| Pyrope ↔ **Verilog** | `foo.prp` + hand-written `foo.v` | `prp-equiv-foo` |
| Pyrope ↔ **Pyrope** | `foo.prp` + `foo_1.prp`, `foo_2.prp`, … | `prp-lec-foo_1`, … |

For the Verilog axis, `prp-equiv-foo` lowers the Pyrope to Verilog and proves the
two equivalent (`lhd lec`, then yosys `lgcheck`). The golden is written by hand
from the same specification, never generated, so it is an independent statement
of what the design must compute.

## Header tags

Every tag is a `:name: value` line inside the leading `/* … */` block.

| tag | meaning |
| --- | --- |
| `:type: equiv` | run the LEC pair (`equiv_slang` for a golden yosys-slang cannot read) |
| `:verilog_top:` | module to compare on the GOLDEN side (default: first module in the `.v`) |
| `:pyrope_top:` | generated module to compare on the Pyrope side |
| `:set: k=v …` | extra `--set` flags, applied to every mode |
| `:reset_style: async` | elaborate implicit resets as async, so the golden can spell an async `always` |
| `:equiv_engine: cvc5` | prove with `lhd lec` only; skip lgcheck (latch/edge shapes it calls different) |
| `:gold_reader: slang` | read the golden through the yosys-slang plugin |
| `:expect_instances:` | instance-count assertion — see `../sim/README.md` |
| `:name_match_only:` | accept a STRUCTURAL state pair — see below |

## Pyrope ↔ Pyrope variants (`prp-lec-*`)

Not every claim has a Verilog counterpart. "These two spellings are the same
design", "rolled and unrolled agree", "this refutes in the index domain" are
claims about two PYROPE sources, and they are written as a **group**, wired by
the file NAME alone:

```
foo.prp      the reference
foo_1.prp    a variant — LEC'd against foo.prp
foo_2.prp    another variant, also against foo.prp (never against foo_1)
```

Each variant gets one `prp-lec-<variant>` target: no script, no BUILD edit, so a
new claim about `foo` is one new file. `foo.prp` keeps its `prp-equiv-foo` /
`prp-statematch-foo` targets if it has a golden `.v` — the axes are independent.

A variant that is **header-only** (tags, no Pyrope code) means *the base source
with MY flags*. That is how a compile-flag differential is written without
duplicating the design; with no flags of its own it becomes the SELF check (one
design elaborated twice, which must reproduce its own structure).

Both sides are handed straight to `lhd lec` when their compile flags agree.
When a variant overrides `:set:`, each side is compiled to its own `lg:` library
first — one `--set` cannot say two things.

| tag | on | meaning |
| --- | --- | --- |
| `:type: lec` | base | marks the group (the target passes `--mode lec` anyway) |
| `:lec_top:` | base | top entity on BOTH sides (default `top`) |
| `:set: k=v …` | both | COMPILE flags for that side; a variant with none inherits the base's |
| `:lec_set: k=v …` | both | extra `--set` for `lhd lec` (base = shared, variant = adds) |
| `:lec_sweep: k=v1,v2` | both | run the pair once per value (repeatable ⇒ cartesian) |
| `:lec_expect: proven\|refuted` | variant | what the run must report (default `proven`) |
| `:lec_grep: REGEX` | both | must appear in the `lhd lec` output (repeatable) |
| `:lec_grep_not: REGEX` | both | must NOT appear (repeatable) |

`:lec_grep:` is what keeps a fixture honest about HOW it was proven: `loop_roll_mixed_1`
would still say PROVEN if the compact-vs-unrolled normalization stopped working
and CVC5 picked up the slack, so it also demands `0 via solver`.

Run a group by hand (name the base for every variant, or one variant alone):

```
./inou/prp/tests/prplec.py inou/prp/tests/equiv/loop_lec_index.prp
./inou/prp/tests/prplec.py inou/prp/tests/equiv/loop_lec_index_1.prp -v   # full lec output
```

Every failure prints the exact `lhd lec` command line it ran, so reproducing one
outside the harness is a copy-paste.

## State-name correspondence

Proving the pair equivalent says the two designs COMPUTE the same thing. It
says nothing about whether they SPELL their state the same way — and that
spelling is load-bearing: hierarchical LEC pairs boxes by name, VCD diffs and
checkpoints are name-keyed, and a structural pairing degrades to `Unknown` the
moment two flops look alike to the matcher.

So every equiv pair also has a `prp-statematch-<name>` target, which runs:

```
lhd compile --emit-dir lg:lg1 foo.prp     # ref  — the Pyrope design
lhd compile --emit-dir lg:lg2 foo.v       # impl — the golden
lhd pass semdiff --stats --ref lg:lg1 --impl lg:lg2
```

Every ref-side register and memory in `semdiff[stats]` must find a counterpart,
**BY NAME** unless the fixture sets `:name_match_only: false` to accept a
structural pair. A design with no registers and no memories has nothing to
correspond and passes silently. A side `lhd compile` will not lower is a
FAILURE, not a skip — the equivalence proof goes through yosys/lgcheck and can
stay green while `lhd compile` refuses the very same file, which is exactly the
hole this check exists to expose. (A refusal that is the WHOLE point of a
fixture belongs in `../errors/`, not here — that is where `latch_rule_a`,
`latch_rule_b` and `reg_clock_from_logic` went.)

**There is no header tag for "this one does not match."** A pair whose state
finds no counterpart FAILS, and the known-broken set is carried by the bazel
`fixme` tag (`_STATEMATCH_FIXME` in `inou/prp/BUILD`) — the repo's one
convention for "red, and we know it". `--test_tag_filters=fixme` runs exactly
that set. A per-fixture header tag would have made them green, which is the
silence this check exists to break.

It is a SEPARATE target from `prp-equiv-<name>` on purpose: a pair can be proven
equivalent and still spell its state differently, and folding the two together
would mean `fixme`-ing a live equivalence proof every time the naming is the
only thing wrong.

### The gap classes

In rough order of how much they are worth fixing:

1. **An unnamed state element on the Pyrope side.** A `pipe` stage flop reaches
   the LGraph with no name at all, so it can never name-pair (`pipe1_pass`,
   `pipe_bare`, `tup_port_pipe` — all three currently pass only because they
   carry `:name_match_only: false`). A real lowering gap.
2. **The golden names the same thing differently.** The `mem_*` family (`t` vs
   `data`), the `latch_*` family (`l` vs `q`), `reg_tuple_reset` (`bank.x` vs
   `bx`). Renaming the golden's signal is the fix; then drop the tag.
3. **Different hierarchy.** The golden puts the state in a submodule the Pyrope
   side keeps flat, or the reverse (`struct_top_port`, `mod_mul_add`,
   `sim_sub_*`). Nothing to pair at the compared top.
4. **Different representation.** `stage[3]` is ONE depth-3 flop against the
   golden's three regs; a ROM written as a `case` has no memory cell at all
   (`mem_rom`, `pipe3_mul`, `mem_whole_*`).
