Use the Pyrope skill to rewrite <source.prp or source directory> into
<destination.prp or destination directory> as clean, idiomatic Pyrope.
An equivalent Verilog or Chisel design may be available at <reference path,
or none>. Preserve the hardware behavior while reconsidering how it is expressed.

First identify the top, supported parameter configurations, existing checks,
and any known equivalence gaps. Run relevant baseline checks before editing so
existing failures can be distinguished from regressions. When using a
Verilog reference, match its parameters and defines; for Chisel, use the RTL
elaborated from the same configuration and record its provenance. If no external
reference is available, compare against the original Pyrope.

Run the source-only style analyzer before editing, especially on large generated
files, and again after each substantial cleanup:

```sh
../livehd/bazel-bin/lhd/lhd pyrope style <file.prp> --diag-fmt pretty
../livehd/bazel-bin/lhd/lhd pyrope style <directory>/*.prp --emit diagnostics:/tmp/pyrope-style.jsonl
```

Inspect the highest-ranked findings first. The analyzer recognizes contiguous
repeated statement blocks, including consistent strides in literals and numbered
identifiers. Use the reported first-copy range and progressions to identify an
indexed collection and a comptime loop; its template placeholders are descriptive,
not executable Pyrope. The default is 20 findings per file; raise `--max-findings`
when the summary reports more. `--max-block-statements` can expose repetitions
whose individual copies exceed the default 128 statements. Findings are advisory:
check dependencies, assignment priority, reset attributes, and old-state reads
before rewriting. Exit zero can include a partial parse; inspect diagnostics.
Neither no findings nor a successful style run establishes correctness or
completeness, and the command does not rewrite code or resolve imports.

Aim for code a person would naturally write from the design's intent:

- Organize by function, such as instruction decoding, memory transactions, and
  arithmetic. Place state near the logic that owns it. Extract coherent modules
  and useful combinational functions; avoid a large block of unrelated register
  declarations at the start of the file.
- Consolidate generated module variants that differ only in widths, field
  types, or configuration into one parameterized implementation. Update callers
  to use it directly and remove obsolete numbered module files. Sharing a body
  underneath a family of unnecessary wrappers, or moving those wrappers into
  one file, is not the completed cleanup. Retain concrete entry modules only
  where an external interface or a verification flow actually requires them.
- Group related fields into tuples and repeated structures into arrays where
  that expresses their meaning. Use packed values for actual bit encodings and
  loops for repeated operations. Choose boundaries that clarify ownership and
  dependencies rather than mechanically grouping names by prefix.
- Before introducing or retaining pack/unpack helpers, check whether the
  consumer can accept the existing unpacked bundle. Change internal submodules
  to take and return typed bundles, including nested bundles and generic
  pipeline storage where useful. Pass a stage's bundle directly to the next
  stage instead of packing it into a bus only to unpack it immediately. Remove
  obsolete adapters, bit offsets, and Chisel-generated `io_xxx` leaf shuffling
  once callers and callees share the meaningful shape. Keep explicit packing
  for real instruction or protocol encodings, bit-vector operations, and required
  external boundaries; document the lane order and widths there. A compiler
  failure on a valid bundle connection calls for a minimal reproducer and a
  LiveHD fix, not an assumption that the hardware requires a packed interface.
- Pass related ports as tuple arguments instead of escaped generated arguments
  such as `` `io_in.control.enable`=enable ``. Reuse an existing tuple directly
  (`child(io_in=next_stage)`) when it already has the needed fields. Otherwise
  construct a named tuple, including nested tuples where appropriate, for
  example `child(io_in=(const control=(const enable=enable), const data=data))`.
  Change intermediate interfaces and their callers together; do not preserve
  flattened generated port spellings merely because they appeared in the RTL.
- Assign whole tuples instead of copying fields one at a time, for example
  `control = decoder.control`. Make producer and consumer agree on field names
  and nesting (`ex_ctrl`, `mem_ctrl`, `wb_ctrl`), and select the named output
  bundle explicitly where needed. A nested return does not bind to a flat
  destination, and a leaf name is not found by searching the tree. Rename a
  callee's generated outputs to meaningful bundle fields and update its callers
  together instead of spreading `io_xxx` names through the design. Subtuples
  are ordinary values: read, pass, and assign `ctl.ex_ctrl` directly instead of
  expanding its leaves.
- Use `if` expressions when every branch selects the value of one destination:
  `result = if select { a } elif other { b } else { c }`. Keep pure branch-local
  calculations inside the expression. Preserve priority with `if`/`elif`;
  use `match` only when its mutually exclusive case semantics are appropriate.
  Do not turn register enables or intentional holds into unconditional writes.

- Replace repeated encodings, masks, field positions, sizes, and configuration
  values with meaningful `comptime const` names. Define them in the file that
  owns their meaning, or export `pub comptime const` values from a shared file
  when several modules use the same encoding. Producers and consumers must use
  the same definitions. Use named types for recurring data layouts where useful.
  Name values for their meaning, not their numeric spelling; ordinary zero
  initialization, boolean conversions, and obvious arithmetic need no artificial
  constants solely to eliminate every literal.
- Use `bool` and `true`/`false` for predicates and control state. Keep numeric
  types for counters, encodings, and bit vectors. Prefer logical operations for
  conditions and make conversions at numeric boundaries explicit.
- Use typed destinations and `wrap` for intentional fixed-width arithmetic,
  for example `wrap add_sub = if subtract { a - b } else { a + b }` with a
  declared `u32` result. Keep slices for extracting fields. Check signedness,
  extension, and shift behavior rather than treating every slice as truncation.
- Express register reset values and polarity in declarations where equivalent,
  for example `reg running:bool:[negreset=true,reset_pin=ref resetn] = false`.
  Retain reset-dependent logic when it also controls enables, holds unreset
  state, or otherwise changes behavior beyond assigning a reset value.
- Use constants or template parameters for configuration, and concrete entry
  modules where required. Preserve supported configurations; explicitly identify
  unsupported ones. Do not expand feature support unless requested.

Preserve cycle timing, register old-value reads, assignment priority, reset and
power-on behavior, memory semantics, and parameter-dependent behavior. Do not
replace unknown or uninitialized state with convenient constants. Intermediate
interfaces may change when that improves clarity; update their callers and
test variants consistently. Preserve the external top-level interface.

Validate incrementally using only LiveHD: compile both sides, run `lhd lec`,
and use Pyrope simulation where needed. When collapsing a field-by-field copy
into a whole-tuple assignment, LEC the collapsed source against the expanded one
it replaced: the two must be equivalent, and that check is cheap enough to run
after each block rather than once at the end. Exercise representative parameter
combinations and interactions, including nondefault configurations touched by
the cleanup. Simulations must check data and run beyond reset into useful work.
Re-run relevant checks on the final source, and check LEC stability across
repeated runs. Simulation supplements an incomplete proof; it does not turn it
into an equivalence proof. Require equivalence at the external top level after
changing module boundaries or interfaces; intermediate modules need not remain
equivalent individually. Preserve the same external top and testbench across
Verilog, generated Pyrope, and cleaned Pyrope2. Preserve an
established unbounded proof rather than accepting only a bounded result after
cleanup. Where the project uses incremental compilation or precompiled imports,
check those paths too, especially after introducing generics or shared files.

Use small comparison adapters only where the verification flow needs to select
or name the same external top, without changing its ports or behavior. If moved or renamed
state needs explicit LEC correspondence, keep that mapping reviewable and report
it separately from automatic matching. Do not weaken assertions, alter the
reference behavior, or change benchmark settings merely to obtain a pass.
When an internal interface changes, translate its existing checks to the new
representation while preserving their assertions, stimulus, and coverage.

Report code-size changes against both the starting Pyrope and matching Verilog,
including every helper introduced or removed. Use the same word-count method and
source scope on both snapshots, state the method, and distinguish formatter-only
changes from reductions in repeated logic. Fewer words are useful evidence of a
simpler representation, not a reason to compress whitespace or weaken validation.
