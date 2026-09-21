# `inou/prp/tests/equiv` — equivalence fixtures

Two axes live here, and a design can sit on both:

| axis | files | target |
| --- | --- | --- |
| Pyrope ↔ **Verilog** | `foo.prp` + hand-written `foo.v` | `prp-equiv-foo` |
| Pyrope ↔ **Pyrope** | `foo.prp` + `foo_1.prp`, `foo_2.prp`, … | `prp-lec-foo_1`, … |

For the Verilog axis, `prp-equiv-foo` lowers the Pyrope to Verilog and proves the
two equivalent with the default `lhd lec` solver and native Slang reader. The golden is written by hand
from the same specification, never generated, so it is an independent statement
of what the design must compute.

Imported helpers may live in a same-stem directory, for example
`imported_bool_cast.prp` imports `imported_bool_cast/leaf.prp`. The automatic
equivalence, state-matching, and Verilog round-trip targets stage those helpers;
only top-level `.prp` files are discovered as fixtures.

## Proof budgets

These small equivalence fixtures require their expected definitive verdict;
timeouts and unsupported encodings fail. The shared `../lec.py` runner sets
`formal.timeout` (20 seconds by default for proof gates) and enforces an outer
watchdog at twice that budget, including setup and loading. A `:set:
formal.timeout=N` override controls both budgets where the harness accepts
`:set:` flags. The Verilog round-trip runner uses `:verilog_check_timeout: N`
for its original-Verilog leg.

Ordinary Slang/Yosys integration uses the same runner with a five-second
internal budget and ten-second watchdog. Those sanity checks may report an
explicit internal timeout as **inconclusive**, but a mismatch, refusal, failed
setup, or watchdog overrun fails. They do not claim proof on timeout.

## Bitfuzz coverage

Each `prp-equiv-*` and `prp-lec-*` target also has a `-bitfuzz` companion.
The companion enables `compile.bitfuzz.mode=wires` for explicit compilation
and the source compilations inside `lhd lec`. It strips internal combinational
width/sign annotations after lowering, before cprop. IO, constants, register
and memory outputs, and instance boundaries are preserved. Cprop must preserve
integer semantics without consulting annotations; the ordinary bitwidth pass
recovers them. The fuzz pass does not infer or restore missing annotations.
Both configurations run under `bazel test //...` with the same equivalence
expectations. A bitfuzz failure remains a failing test.

Run one manually with:

```
python3 inou/prp/tests/pyrope_test.py -i inou/prp/tests/equiv/trivial_if.prp --bitfuzz
```

## Header tags

Every tag is a `:name: value` line inside the leading `/* … */` block.

| tag | meaning |
| --- | --- |
| `:type: equiv` | run the LEC pair (`equiv_slang` compares both emitted Verilog sides) |
| `:verilog_top:` | module to compare on the GOLDEN side (default: first module in the `.v`) |
| `:pyrope_top:` | generated module to compare on the Pyrope side |
| `:compile_top:` | optional source top to materialize explicitly during Pyrope compilation, including a defaulted generic among multiple public templates |
| `:set: k=v …` | extra `--set` flags, applied to every mode |
| `:reset_style: async` | elaborate implicit resets as async, so the golden can spell an async `always` |
| `:verilog_check_timeout: N` | v2prp2v only: seconds for the original-Verilog LEC leg (default 20); a timeout fails the test |
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

So every STATE-BEARING equiv pair has a `prp-statematch-<name>` target, which runs:

```
lhd compile --emit-dir lg:lg1 foo.prp     # ref  — the Pyrope design
lhd compile --emit-dir lg:lg2 foo.v       # impl — the golden
lhd pass semdiff --stats --ref lg:lg1 --impl lg:lg2
```

Every ref-side register and memory in `semdiff[stats]` must find a counterpart,
**BY NAME** unless the fixture sets `:name_match_only: false` to accept a
structural pair. A design with no registers and no memories has nothing to
correspond, so it is EXCLUDED from the axis by `_STATEMATCH_COMB` in
`inou/prp/BUILD` rather than run as a silent pass (measured 2026-09-21: 190 of
319 pairs were vacuous this way). A pair whose claim IS that no state appears
stays out of that exclude list so it keeps running. A side `lhd compile` will not lower is a
FAILURE, not a skip — the equivalence proof goes through yosys/lgcheck and can
stay green while `lhd compile` refuses the very same file, which is exactly the
hole this check exists to expose. (A refusal that is the WHOLE point of a
fixture belongs in `../errors/`, not here — that is where `latch_rule_a`,
`latch_rule_b` and `reg_clock_from_logic` live.) The old exhaustive-match
false-positive reproducer is now `latch_match_exhaustive`, paired with a
combinational reference; `latch_match_partial` checks that a reachable hold
path still retains its latch.

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

For a hierarchical Pyrope design with a flat golden, set
`:state_match_flatten: true` together with `:pyrope_top:` and `:verilog_top:`.
The harness flattens both selected tops with `lhd pass color flat` before
matching their state by instance-qualified name. This changes the comparison
scope, not the requirement that every register and memory find a counterpart.
`generic_tuple_register` uses this mode with field-named golden registers;
its separate `lhd/tests:lec_tuple_register_test` retains the packed golden and
checks unbounded equivalence plus behavioral mutations.

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

The `lec/` subdirectory extends the numbered-variant convention to standalone
Verilog soundness cases, including expected refutations. `roundtrip/` contains
Pyrope writer fixtures checked against their source. Both, together with the
synthesis corpus in `../abc/`, join `//inou/prp:integration` automatically.
