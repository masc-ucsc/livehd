# `inou/prp/tests/equiv` — Pyrope ↔ Verilog golden pairs

Each `foo.prp` has a hand-written `foo.v` twin. `prp-equiv-foo` lowers the
Pyrope to Verilog and proves the two equivalent (`lhd lec`, then yosys
`lgcheck`). The golden is written by hand from the same specification, never
generated, so it is an independent statement of what the design must compute.

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
