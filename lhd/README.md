# lhd developer tools

`lhd` is the stateless driver for compile, simulation, equivalence, and synthesis.
Use `lhd help` or `lhd describe <command>` for the command's accepted arguments.

## Formal regression policy

Ordinary regressions use `lhd lec` with the default solver and native Slang
reader. `formal.strict`, `formal.lec.gold_reader`, and
`formal.lec.gate_reader` are removed options. UNKNOWN always exits nonzero,
as does a vacuous assert or internal assume in `formal verify`. REFUTED keeps
its distinct counterexample exit class. Bounded successes disclose their depth;
cache hits preserve that scope and reject legacy records without it.
The explicit debug setting `formal.solver=lgyosys` also runs default native
LEC and requires agreement with lgcheck; it cannot replace the native proof.
One regression tier additionally calls `inou/yosys/lgcheck` DIRECTLY as a second
opinion: `//inou/prp:prp-equiv-*` (Pyrope-generated Verilog vs its hand-written
golden), so a native LEC that ever proved nothing cannot go unnoticed across
that corpus. There a yosys refutation fails the test while an inconclusive or
timed-out yosys is tolerated — see `inou/prp/tests/equiv/README.md`. Every other
LEC in the repository stays on the default cvc5 solver.

The Yosys importer regressions explicitly compile through Yosys and require
both `lgcheck` and native `lhd lec` on each checked round trip. The `nocheck_`
fixtures retain their existing compilation-only scope.

Memory entry and bulk writes preserve their process clocks, including uniform
negative edges. Mixed-edge memories retain an explicit marker in LGraph;
formal checking and Verilog emission reject that unsupported schedule. Bulk
updates combined with entry writes require one shared clock and edge.
Native formal also rejects uniform falling-edge memory schedules, although
their Verilog emission is supported and covered by RTL simulation.

## Simulator tuning (`sim.tune.*`)

Knobs that change only simulator speed, never a simulated value, live under
`sim.tune.*`: `dirty`, `fence`, `live_words` and `backend`, each `auto` by
default. `sim.tune.profile=auto|on|off` (default `auto`) decides whether
`lhd sim` learns them per workdir. With a user `--workdir` and
`lhd.incremental=true`, an `auto` run profiles until the workdir's decision
converges, trying at most one better vector per setup and keeping it only when
it is at least 7% faster with byte-identical results. A trial that cannot be
judged (no comparable run, a failed build, a crash) is reverted at the end of
that run, and after two such attempts on one design structure `auto` stops
trying it. An observation run (`--list-signals`, `--restart-cycle`, VCD) does
not learn but builds the tuned vector, so it replays the same binary. A profiling run
zero-fills `?` literals unless `sim.unknown_zero` is set, and takes no
checkpoints. Per knob, an explicit `--set` beats `sim.tune.file`, which beats
the workdir's decision (`<workdir>/incr/scopes/sim/<scope>/tune.jsonl`), which
beats the built-in default, `d=on f=16` (dirty gating on; a design profiled
busy is trialed back to `d=off`, and small always-toggling benches can pin
`--set sim.tune.dirty=off`). `off` ignores the workdir's data.
`sim.tune.export=FILE` writes the applied vector for `sim.tune.file` to pin
elsewhere. The envelope's `sim_tune.reproduce` is the `--set` list that
rebuilds the applied vector.

The old spellings are rename errors: `sim.color_dirty` -> `sim.tune.dirty`
(`true`/`false` -> `on`/`off`), `sim.fence_ratio` -> `sim.tune.fence`,
`sim.live_words` -> `sim.tune.live_words` (`0` -> `auto`), and `sim.backend`
-> `sim.tune.backend`. A test or benchmark that needs checkpoints, random `?`
fill, or a fixed generated tree across runs in one workdir pins
`--set sim.tune.profile=off`. The full guide is `docs/simopt.md` §13.

## Pyrope style suggestions

```sh
./bazel-bin/lhd/lhd pyrope style file.prp --diag-fmt pretty
./bazel-bin/lhd/lhd pyrope style file.prp --emit diagnostics:style.jsonl
```

`pyrope style` parses the source with Tree-sitter, without compiling or resolving
imports. It reports repetition and three structural cleanup opportunities. All
findings are advisory; they do not establish that a replacement is equivalent.

The repetition detector finds **contiguous repeated sequences of statements within a scope**.
One copy can include several declarations, assignments, and nested `if` bodies;
physical line breaks do not determine the block boundaries. Comments and
whitespace do not affect matching.

For example, a repeated sequence of `const t0 = data[0]`, `const t1 = t0 + 1`,
and `out[0] = t1` can match the next copy using `t2`, `t3`, and `out[1]`.
The whole three-statement sequence is reported together. Numeric literals and
digit runs in identifiers must each remain fixed or advance by a consistent
stride across copies. Decimal, hexadecimal, and octal integer spellings compare
by value. Operators, types, string contents, and the remaining identifier text
stay exact. Signed bit patterns, unknown bits, scaled literals, and integers
outside the detector's bounded arithmetic range stay exact as well.

Findings include the full source range, the first-copy range, statements per
copy, repetition count, an illustrative template, and the observed progressions.
`{p0}`, `{p1}`, etc. in a template are descriptive placeholders, not Pyrope
syntax. A progression such as `p0 = 4 + (2 * i)` uses a zero-based copy index.
Templates are limited to 1200 bytes and explanations to eight progressions;
the first-copy location always points to the full source.

Suggestions are ranked by estimated removable syntax tokens. Overlapping
suggestions from the same detector are suppressed; different rules may report
the same region. Ties use block size and stable source ordering. Numeric progression earns
the `likely-unrolled-loop` rule ID; exact repetition uses `repeated-code`.
These are heuristic suggestions, not proof of the original Verilog's structure
or of a replacement loop's equivalence. Numbered scalar names may need to become
an indexed collection before refactoring. The command never rewrites source.

| Option | Default | Meaning |
| --- | --- | --- |
| `--min-repeats N` | 7 | Repetition only: minimum complete copies; at least three are needed to check a stride |
| `--max-block-statements N` | 128 | Largest number of sibling statements in one copy; maximum 4096 |
| `--max-findings N` | 20 | Highest-ranked findings across all rules shown per file |

The additional rules are enabled by default and do not use `--min-repeats`:

- **`whole-tuple-copy`** finds at least two consecutive plain assignments with
  matching field paths, such as `dst.valid = src.valid; dst.data = src.data`.
  Nested fields identify the deepest shared bundle prefixes. The suggestion is
  to consider `dst = src` after checking complete field coverage, compatible
  types, and assignment semantics, then LEC it against the expanded copy.
  Indexed accesses, same-root copies, duplicate/overlapping fields, conversions,
  declarations, and modified assignments are excluded. Intervening statements
  break the sequence; comments do not.
- **`flattened-bundle-arguments`** groups escaped dotted call arguments such as
  ``child(`io_in.control.enable`=enabled, `io_in.data`=data)`` by their first
  path component. Even one such argument can suggest a structured bundle.
  Update the callee interface and its callers together; the analyzer does not
  resolve that interface. Ordinary dotted arguments, underscore-only names,
  and already structured tuples are not flagged. Each group is anchored on its
  first argument, with related locations identifying its contributing arguments.
- **`single-destination-conditional`** finds standalone exhaustive
  `if`/`elif`/`else` statements whose branches each contain one plain assignment
  to the same identifier or static field path. It suggests an `if` expression
  preserving branch order. Missing `else`, `unique if`, initializer clauses,
  declarations, branch-local calculations, calls in branch values, indexed
  destinations, compound assignments, and `wrap`/`sat` are excluded.

New findings include `score` plus `destination`/`source`/`field_count`,
`bundle`/`argument_count`, or `destination`/`branch_count`, respectively.
Related locations are capped at eight per finding; counts include all matches.
Existing repetition findings retain their template/count/progression attributes.

Findings and a per-file summary use the normal diagnostic stream: human text on
stderr with `--diag-fmt pretty`, JSONL with `--diag-fmt json`, and a structured
file with `--emit diagnostics:PATH`. JSON records include spans, related notes,
and rule-specific `attrs`. `-q` suppresses stderr while
preserving the declared diagnostics file. There is no stdout result envelope.

Syntax errors produce a `partial-analysis` warning; intact sequences are still
checked, and error-containing statements break candidate sequences. Suggestions
and partial parses exit zero. Input or parser infrastructure failures exit
nonzero; later files are still checked.

The repetition detector does not match reordered or scattered statements, arbitrary
identifier renamings, or irregular iteration progressions. It does not merge
sequences across scope boundaries. The source-only analyzer in
`pyrope_style.hpp` returns a report independently of CLI presentation, so future
rules and editor integration can share it.

Run the focused regressions with:

```sh
bazel test -c opt //lhd/tests:lhd_style_test //lhd/tests:lhd_fmt_test //lhd/tests:lhd_help_test
```
