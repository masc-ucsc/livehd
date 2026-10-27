# inou/slang — direct SystemVerilog → LNAST reader

The direct `--reader slang` front-end: slang v12.1 elaborates SystemVerilog and
`Slang_context` lowers the AST straight to LNAST, one Lnast per module in the
extracted unit form (`top -> [io, stmts]`, `lambda_kind="mod"`, exact Verilog
module names) so the standard upass pipeline (SSA → io_meta → tolg) compiles
it like any Pyrope unit. This is the default SystemVerilog reader and supports
compilation and synthesis as well as `ln:`/`lnast-dump:` inspection. Use
an explicit `lhd compile --reader yosys ... --emit-dir lg:comparison/`
for importer debugging. Compare those graphs against a native Slang `lhd lec`
run; Yosys is an additional cross-check, not a substitute for native coverage.

Clocked blocking assignments keep a process-local current value: later
statements see earlier writes, and the process commits the final value to its
registers. Integral-element unpacked arrays use a current array snapshot while
their persistent storage retains its identity and old-value reads in other
processes. Blocking arrays require a single writer; struct-element arrays still
diagnose. Loop counters used only inside canonical `for` loops remain mutable
elaboration controls, including module-scope integers beneath runtime enables.
The unroller writes back their terminal value; counters observed outside their
loops retain normal state analysis. Actual conditional data writes still infer
latches. Memory write ports retain
the clock of their owning process. Mixed write-edge polarities are represented
with the Memory IR's mixed-edge marker and diagnosed; formal refuses that
unsupported schedule by name rather than silently collapsing its edges.

Pyrope-only emission preserves integer and string module parameters as generic
defaults (`pub mod core<N=8, MODE="fast">...`). Each Slang-elaborated
specialization carries its bound values, including instance overrides; localparams
remain body constants. These are parameters of the emitted, elaborated body:
port widths, generate branches, and other structural choices have already been
resolved by Slang. To change those choices, re-elaborate the Verilog configuration.

Two limitations of that header are real and currently unchecked:

* **Only a parameter that still appears SYMBOLICALLY in the emitted body is
  honoured on override.** A parameter that reached an elaboration-time decision
  is baked in: `assign y = (N == 2) ? a + 1 : a + 2` emits the taken arm
  unconditionally, yet the header still advertises `<N=2>`, so a Pyrope-side
  `core<N=3>(...)` compiles and produces DIFFERENT hardware than the Verilog with
  the same override (it LECs REFUTED, with no diagnostic). Every string parameter
  is in this class — Slang consumes string expressions during elaboration.
  A parameter used only in arithmetic (`assign y = a + N`) does round-trip
  correctly, and that is what `lhd/tests/slang_param_provenance_test.sh` covers.
* **Recompiling the emitted Pyrope renames every parameterized module.** A
  generic header makes the re-read unit a deferred TEMPLATE, so each call site
  mints a specialization clone named `subm__u1_u8_N_3_h007714d1`. Direct
  `verilog:` -> `lg:` is unaffected, but on the `verilog:` -> `pyrope:` -> `lg:`
  leg `canonical_entity_name` cannot undo the mangling, so cross-frontend def
  pairing dies (measured on a two-instance hierarchy: `2 ref-only def(s)`,
  `registers ref 0/2 paired`, verdict `DIFFERENCES present` on an equivalent
  design) and hierarchical LEC degrades to one flat whole-design proof.
  The fix is to give an IDENTITY specialization — every generic bound to its own
  declaration default, no injected port type, no var-arg — the module's own name,
  which is the rule `specialize_top_defaults` already applies to a generic top:
  one parameter value-set means one LGraph, and it is the module. Doing that at
  a call site additionally requires the emitted instance call to resolve to the
  CONCRETE twin rather than the template; naming the clone after the template
  without that makes `lookup_callee` return the template again, which
  re-specializes the same statement forever (observed as a stack overflow in
  `hhds::Source_locator::find_file`, via the scratch-locator `base_` chain the
  loop grows).

## Constructs that look like state but are not (or the reverse)

- **Run-time severity tasks.** `$info`/`$warning`/`$error`/`$fatal` inside a process drive no
  hardware, exactly like `$display`; they are ignored with one `severity-task-ignored`
  warning per call instead of failing the design (FlooNoC carries a `$warning` in an `always`
  outside `TARGET_SYNTHESIS`). The elaboration forms in a module or generate scope are slang's
  own diagnostics (`InfoTask`, ...) and are unchanged: a taken `$error` still fails.
- **Nonblocking assignments in a level-sensitive `always @(*)`.** The Migen/LiteX style
  `x <= 0; if (c) x <= y;` assigns on every path, so `x` is combinational, as in yosys: a
  latch comes from an incomplete path, not from the `<=` spelling. Such a variable is plain
  logic when the block writes it only with `<=` on every path and never reads it back (a
  nonblocking read of the old value and the settled value would differ). An incomplete path,
  a mixed `=`/`<=` writer, a read-back and `always_latch` keep the latch classification (the
  prim_clk_gate idiom `always @(clk or ...) if (!clk) en_latch <= ...`). Chains of
  degenerate latches used to trip the latch contract (rule C) on LiteX output.
- **`inout`.** LiveHD has no tri-state cell, so an `inout` port is refused with
  `unsupported-inout-port` unless `--set compile.slang.inout_as_driven=true` selects the pad
  model used by ASIC flows: the port is lowered as the direction its module uses, an OUTPUT
  when the module (or a child instance wired to the port) drives it and an INPUT when it only
  reads it. A `1'bz` drive is a don't-care, so the driven value wins and a read of the port
  sees what the module drives (the external value of a genuinely bidirectional pad is not
  modeled). A Lattice `TRELLIS_IO` (`assign B = T ? 1'bz : I; assign O = B;`) becomes an
  output `B` carrying `I`, and the bus it drives is an output of the parent.

## Passing raw slang driver args (`-- ...`)

Everything after a `--` on the `lhd` command line rides straight to the slang
driver, so the standard slang options work — in particular `-F`/`-f` command
files (verilog file lists):

```
lhd compile --reader slang --top gcd -- -F ../simplechisel/build_gcd/filelist.f
```

The sources may come entirely from the file list, so a positional
`verilog:`/`*.v` input is optional in that case (`files` and the raw flags are
both accepted; at least one must supply sources). lhd hands the args to
`inou.slang` as the `slang_flags` label (`\x1f`-separated so shell tokens like
`+incdir+a,b` survive). An existing `+incdir+PATH` directory is treated literally,
including `+` characters in Bazel canonical repository names. Otherwise the
standard `+incdir+dir1+dir2` list syntax applies; `-I PATH` is unambiguous. Covered by `tests/slang_filelist.sh`
(`bazel test //inou/slang:slang_filelist`).

Fixed unpacked structs with integral leaves lower to a deterministic bitstream
layout (declaration order, most significant field first). Nested structs, fixed
arrays inside them, whole copies, field writes, and typed module connections
share that layout. Dynamic array selectors retain bounds checks and signed
fields retain their signed interpretation. Dynamic containers and non-hardware
leaves such as strings still require a diagnostic.

`--ignore-unknown-modules` retains missing definitions as typed, opaque graph
interfaces using the existing port-direction inference. The interface is the
union of every instance's connected ports at the widest connected width; a port
inferred as both input and output, or a lone connected output on one instance
while another connects more, is rejected. This does **not** infer memory behavior from SRAM
names: include the behavioral SRAM `.v` models in the file list to obtain
LGraph `Memory` nodes. A Liberty-only macro remains an opaque physical cell.

## File map (CIRCT ImportVerilog-shaped split)

| file | concern |
|---|---|
| `inou_slang.cpp` | EPRP entry (`inou.slang` / `inou.verilog`), per-file driver loop, options |
| `slang_driver.cpp` | embedded slang v12.1 driver; diagnostics → `diag::Sink` |
| `slang_context.{hpp,cpp}` | the one `Slang_context` class: naming, const-eval tiers, bool/int kind discipline, provenance, diags |
| `slang_structure.cpp` | modules, ports/io, process classification (comb/ff/async-reset), dataflow-ordered driver emission, instances, generate |
| `slang_stmt.cpp` | statements: if, case→`if`/`unique_if`, capped slang-side loop unrolling |
| `slang_expr.cpp` | rvalue expressions; reductions expanded inline; selects via shift+const-mask |
| `slang_lvalue.cpp` | assignment targets: vars, packed selects (half-open `set_mask` endpoints, clipped to the selected container), concat split, memory element writes |
| `slang_types.cpp` | type info + the single materialize-conversion seam (`trunc_to`/`fit_wrap`/`to_pattern`) |
| `slang_location.hpp` | slang SourceRange → diag span + hhds SourceId minting |
| `slang_diag.hpp` | slang DiagnosticClient → LiveHD diag sink |

Key invariants the lowering maintains:

- every lowered value sits in the integer range of its slang type; width/sign
  changes go through `materialize_conversion` only;
- comparison/logical results are LNAST **bool**; anything reaching an integer
  context materializes to 0/1 (`to_int_value`) — and single-bit `get_mask` is
  never used for value extraction (Dlop's `x#[i]` is a −1/0 boolean);
- drivers (assigns/processes/instances) emit in dataflow dependency order so
  wire reads are plain; combinational cycles fall back to settled reads
  (`delay_assign +1`, LNAST-tier only);
- registers are `declare(…,'reg')` hoisted to module start (an `output reg`'s
  q pin IS the output); non-`clk/clock` or negedge clocks ride per-reg
  `clock_pin`/`posclk` attrs; extracted async-reset rungs become
  `initial`/`reset_pin`/`sync`/`negreset` attrs. Constant reset assignments to
  concatenations split recursively by destination width and signedness;
  packed slices use the same offsets as ordinary writes. Synchronous constant
  reset conditionals also preserve these attributes when several independent
  nonblocking conditionals share one clocked block. Their original guards stay
  in the data logic; blocks with overlapping destinations or blocking writes
  retain ordinary procedural lowering. A constant scalar declaration
  initializer or simple `initial q = CONST` on a register without such a reset
  becomes the implicit-module-reset value and emits `initial-without-reset`,
  because formal equivalence can differ from reset-less hardware. A register
  ARRAY's constant `initial`-block contents are power-on contents only
  (`initial=<packed contents>` next to `= nil`, docs 08-memories), so no
  reset is bound. Entry 0 is the element at the array's lower bound (`mem [1:4]`
  packs `mem[1]` lowest), and each value keeps its element width and sign;
- unpacked arrays lower to the `comp_type_array` declare + `store(mem,idx,v)`
  / `tuple_get(d,mem,idx)` memory vocabulary with `fwd=0` (Verilog
  nonblocking reads see old contents). An index whose type can leave the
  declared range (`reg [7:0] mem [2:9]` at a 4-bit address, a depth that is not
  a power of two, a wider address) spells the Verilog out-of-range rule out in
  the LNAST (`build_unpacked_address`): a write sits under the range check (an
  out-of-range write does nothing, never aliasing an entry), and the index is
  the offset's low clog2(depth) bits, so an out-of-range read returns the entry
  those bits select (what yosys' memory_map returns; any value refines X) and
  0 past a depth that is not a power of two. A whole read of a packed 2-D reg
  kept as a memory is its whole bit view (`q#[0..=N*W-1]`). The Pyrope writer
  re-emits both as ordinary code, so regenerated Pyrope needs no Verilog
  relaxation;
- a runtime-position packed write (`w[b] = x`, `w[b -: 4] = v`, an element of
  a combinational array kept as one packed bus, `arr[j][c +: 4] = v`) is a
  range `set_mask` (`w#[lo..=hi] = v`), blocking or nonblocking. A select
  chain flattens onto the root bus, so `resolve_packed_lvalue` records what
  each select leaves: a dynamic element index that can leave its array guards
  the write (Verilog writes nothing), and a part select that can leave its
  container (a member, an element) is clipped to it (`clip_window`: only the
  in-range bits are written, never the neighboring element's). A select of
  the root itself needs neither unless its position can be negative: the
  write already stops at the root's declared width. Whether a select can
  leave is judged by its VALUE (`selector_bounds`: `k*8` of a 2-bit `k` is
  0..24), not by its 32-bit type, so the byte-lane idiom stays one plain
  window. Every emitted position is non-negative by construction;
- a partial write into a variable stored as per-field leaves (a packed-struct
  local or function local, a bundle port) splices a copy of the fields the
  write can reach and stores those fields back (`emit_leaf_split_rmw`); the
  whole name is a phantom nothing reads;
- a runtime-indexed packed 2-D reg becomes a memory only when its range is
  DESCENDING: an ascending `[0:N-1]` array packs element 0 in its most
  significant bits, which a memory's packed view (entry 0 at bit 0) would
  mirror, so it stays a flat register.

Constant packed bit-selected clocks become named one-bit clock wires. Packed
vectors written by these processes lower to scalar register bits, each with
its own clock edge and reset attributes; a packed view preserves whole reads
and output ports. Their writes currently require constant nonblocking
destinations. Dynamic clock selectors, blocking writes to the split vectors,
and overlapping writes from different clocks are diagnosed.

Concurrent constant packed-bit/slice assignments and instance outputs lower to
one single-assignment wire per bit. Constant reads use those wires directly;
whole-vector and runtime-indexed reads use one assembled vector wire. This
preserves generated ready chains regardless of source order, including ascending
ranges and mixed slice/instance drivers. Overlapping static drivers warn and keep
the legacy source-ordered lowering (an arrayed instantiation broadcasting one
output onto one net bit is that shape); procedural, state, and dynamic
destinations retain their existing lowering too.

Counted `while` scans support runtime early exit when an adjacent constant
initializer, a conjunctive `counter < constant` bound, and a unique unconditional
unit increment prove termination without overflow. They lower to guarded
iterations within the normal unroll budget; arbitrary runtime loops still
diagnose. The counter's terminal value and all body side effects are preserved.

Constant unpacked parameter arrays can be passed to runtime functions. Partial
multidimensional array selects can supply subarray ports: remaining elements
are packed in declaration order, independently of native memory index order.

## Fixture discovery

A new ordinary regression is a `.v` or `.sv` file in `tests/sv/`; Bazel creates
`slang_<stem>` automatically. Name the top module after the file, or select it with `// :top: entity`. The shared
`tests/slang_compile.sh auto FILE` runner compiles it through native Slang and
compares the emitted Verilog with the source using default `lhd lec`.

Optional leading comments select additional expectations:

- `// :test: roundtrip` also emits Pyrope, recompiles it, and compares that result.
- `// :test: lec_no_x` also rejects introduced X/Z literals.
- `// :test: error` requires a clean compiler error with a structured diagnostic;
  optional `// :error: REGEX` matches the diagnostic message.
- `// :test: roundtrip_sim` value-checks the Pyrope round trip instead of
  running LEC (see below).
- `// :lec_timeout: N` raises the per-fixture LEC budget (watchdog 2N).
- `// :lec_solver: NAME` runs `lhd lec --set formal.solver=NAME`. With
  `lgyosys`, the native proof is cross-checked by lgcheck under
  `LGCHECK_EQUIV_TIMEOUT=<lec_timeout>`, and the fixture requires lgcheck's
  unbounded proof (crosscheck `exit_code` 0), not a bounded-clean window. Add
  the fixture to `_SV_LGYOSYS` in `BUILD` so it stages `//inou/yosys:scripts`.

These tests share the five-second internal LEC budget and ten-second watchdog
below. Dedicated tests remain for warning details, generated storage complexity,
file lists, determinism, and other behavior a value comparison cannot check.

## The coverage ladder (todo/ 2s subtask E)

`slang_ladder.bzl` pins each listed `inou/yosys/tests/*.v` source (those not
subsumed by a larger sibling fixture) to its strongest passing tier;
`tests/slang_compile.sh <tier> <file>...` enforces it
both ways (a regression fails, and an outgrown `error` entry fails until the
ladder is promoted). Tiers:

- `lec` — slang→LNAST→tolg→cgen Verilog, LEC-checked (`lhd lec`, default solver) against
  the source. The strongest tier. Slang tests leave the solver at its default;
  `lgyosys` is reserved for explicit cross-checks of the native LEC engine.
  The ladder uses `formal.timeout=5` with a ten-second outer watchdog. An explicit internal
  timeout reports `LEC check: inconclusive`; equivalence remains unproven.
  Refutations, unsupported encodings, setup errors and watchdog overruns fail.
- `verilog` — compiles to Verilog; a known LEC gap is tracked in the ladder
  comment next to the entry.
- `lnast` — LNAST + `ln:` save/reload round-trip only.
- `error` — must fail cleanly with a structured diagnostic (used for the
  non-LRM sources slang correctly rejects, e.g. procedural writes to nets).

Run one: `bazel test //inou/slang:slang_compile-<name>`; all:
`bazel test //inou/slang:all`.

Current tally (2026-06-17, 116 sources): **86 lec**, 6 verilog, 2 lnast,
21 error (9 of those are non-LRM sources slang correctly rejects, e.g.
procedural writes to nets; the rest are tracked feature gaps: instance arrays,
hierarchical punch-through references, `'bx` golden arms, dynamic
mem-element part-selects). The 6 `verilog`-capped entries are LEC-slow or
genuine gaps: two big-memory / wide-arith tests (`long_mem3`,
`long_nocheck_iwls_square`) and `fixme_sha256`'s wide
reduction are deliberately capped because LEC is slow there (the small-array
coverage simple_rf1/rf2, tuplish, fixme_array carries the memory guarantee);
`mem_sync_init` and `nocheck_slang_foreach` are real memory-lowering gaps.
`long_nocheck_iwls_square` now attempts LEC with the five-second budget and
accepts an explicit timeout as described above.
Correct mixed signed/unsigned arithmetic and narrow (1- and 2-bit) signed
port/temp ranges (`add1`, `issue_047`, …) are now LEC-verified — a signed
operand in an unsigned expression zero-extends, and `materialize_conversion`
plus `int_min_str`/`int_max_str` carry the 1800 §11.8.2 sign rules.

The unknown-module (liberty cell instance) policy is pinned by the
`slang_blackbox` target; `tests/verilog/sky130_test.lib` is kept as the sky130
liberty sample.

## Which Verilog constructs to support (triage rule)

When a SystemVerilog construct fails to lower, decide whether it is a bug to fix
or a test to drop using three reference points — the standalone slang frontend,
the integrated Yosys Slang frontend, and our native reader:

1. **slang native frontend rejects it → out of scope, for sure.** slang v12.1 is
   the strictest valid-SV gate; if its frontend cannot elaborate the construct it
   is either illegal SV or non-elaboratable, so `--reader slang` has nothing
   well-formed to lower. Drop it (or keep it as an `error`-tier test when slang
   rejects it *cleanly*).
2. **`read_slang` handles it → we must support it too.** A construct yosys's
   built-in `read_slang` lowers is a real, synthesizable RTL idiom; both
   `--reader slang` and `--reader yosys` should match it. A failure here is
   a genuine bug to fix.
3. **`read_slang` fails but slang native succeeds → it depends.** The construct is
   valid SV (the frontend accepts it) but the integrated Yosys Slang frontend's RTLIL
   conversion cannot handle it. Try to support it in our native lowering —
   unless it is a non-synthesizable construct (`$foo` system tasks,
   simulation-only strangeness), in which case dropping the test is fine.

### Running the three checks

```
# 1. standalone slang frontend  (valid-SV gate; NOT always installed locally)
slang foo.sv

# 2. bundled Yosys with integrated Slang, no separately loaded plugin
./bazel-bin/inou/yosys/yosys2 -p "read_slang foo.sv"

# 3a. LiveHD native reader (slang-library based)
lhd compile --reader slang       foo.v --emit-dir pyrope:out/
# 3b. LiveHD Yosys reader
lhd compile --reader yosys foo.v --emit-dir verilog:out/
```

**When standalone `slang` is not installed**, use `--reader slang` as a proxy for
check 1: the native reader embeds the same slang v12.1 frontend, so a clean
elaboration that merely hits a lowering gap surfaces as an `unsupported-*`
diagnostic (`"pass":"inou.slang","category":"unsupported"`), whereas a frontend
rejection surfaces with slang's own diagnostic code (e.g.
`AssignmentPatternNoContext`). So an `unsupported-*` from `--reader slang` ≡
"slang native succeeds" (case 2/3); a slang frontend code ≡ "slang native
rejects" (case 1).

Worked examples (`inou/prp/tests/equiv/`), both now supported by `--reader slang`:

| test | standalone slang | yosys-slang | `--reader slang` | equiv test |
|---|---|---|---|---|
| `hier_value.v` (`HierarchicalValue`) | ✅ | ✅ | ✅ (read as a `ValueExpressionBase`, `slang_expr.cpp`) | `:type: equiv` (prp-equiv + prp-v2prp2v round-trip) |
| `assign_pattern.v` (`SimpleAssignmentPattern` lvalue) | ✅ | ❌ | ✅ (`assign_to_pattern`, `slang_lvalue.cpp`) | `:type: equiv_slang` (yosys can't read the `'{...}` lvalue) |

### equiv testing: `:type: equiv` vs `:type: equiv_slang`

`inou/prp/tests/equiv/<name>.{prp,v}` golden pairs are LEC'd top-only. The `.prp`
header's `:type:` selects how the readers are exercised:

- **`:type: equiv`** (default, `prplib.run_equiv`): the `.prp` lowers to Verilog
  and lgchecks against the golden `.v` (yosys `read_verilog`). The matching
  `prp-v2prp2v-<name>` target additionally round-trips the `.v` through
  `--reader slang` → Pyrope → Verilog, checking the emitted Pyrope against the
  hand-written twin and the generated Verilog against the original. Use when
  yosys can read the `.v` (e.g. `hier_value.v`, plain module name).
- **`:type: equiv_slang`** (`prplib.run_equiv_slang`): for a `.v` that yosys's
  `read_slang`/`read_verilog` cannot ingest (e.g. `'{...}` assignment-pattern
  lvalues in `assign_pattern.v`). The `.v` is read by the NATIVE `--reader slang`
  into clean cgen Verilog (implementation) and lgcheck'd against the
  `.prp`-generated Verilog (reference) — so only `--reader slang` is on the hook,
  and no yosys read of the original `.v` is needed.

The `:pyrope_top:`/`:verilog_top:` header tags pin the (differently-named) top on
each side. Pick `equiv` when both readers can read the `.v`; `equiv_slang` when
only the native slang reader can.

Run the automatically discovered SV corpus with
`bazel test -c opt //inou/slang:integration`. Each fixture remains an individual
Bazel test, so failures identify the source and runtimes stay bounded, except the
`// :test: error` clean-rejection sets that run as one target apiece through
`slang_compile.sh auto FILE...` (`slang_const_clock_rejects`,
`slang_derived_clock_rejects`, `slang_reader_rejects`; each failure still prints
`FAIL(<top>)` naming the fixture). A new fixture is grouped by adding it to the
matching `_SV_*_REJECTS` list in `BUILD`.

`// :test: roundtrip_sim` emits and recompiles Pyrope, then checks the
re-emitted Verilog without LEC. This profile covers behavior the formal encoder
cannot represent, such as flop-driven clocks; it does not claim a proof or
accept an unsupported LEC verdict. Ordinary roundtrips still use LEC.

1. Every `// :verilog_re: ERE` header must match the re-emitted Verilog and no
   `// :verilog_not_re: ERE` may (for example, an asynchronous reset must keep
   its sensitivity list in every block).
2. The sibling `<stem>_tb.prp` Pyrope testbench (`import("lg:<top>")`, `tick`
   directed vectors, `assert`) runs under `lhd sim lg:` with
   `sim.unknown_zero=true`. Native simulation is cycle-based: each `step` is
   one reference-clock cycle, so edge polarity, a reset asserted between edges,
   and sub-cycle toggles are not observable there. Pin those with step 1.
3. A design native simulation refuses declares `// :sim_unsupported: TEXT`
   instead of a `_tb.prp`; the refusal (exit 7, class `unsupported`, `TEXT` in
   the message) is required, so a newly supported schedule fails until it gets
   a testbench.
4. An optional `<stem>_tb.v` event-level bench (it must `$fatal` on mismatch)
   runs under Icarus only with `LHD_EXTERNAL_SIM=1`, for example
   `bazel test --test_env=LHD_EXTERNAL_SIM=1 //inou/slang:slang_writer_divclk`
   with iverilog/vvp on `PATH`. The default suite needs no external simulator.

The discovered integration cases reserve four Bazel CPUs each so concurrent
frontend/solver jobs do not exhaust the fixed LEC watchdog through resource
contention. Synthesis integration cases in `//inou/prp:integration` run exclusively.

For source bundles containing unfinished modules outside an explicit top hierarchy,
pass `-- --ignore-uninstantiated-modules` or `--set compile.slang.ignore_uninstantiated_modules=true`
(the form a flow that cannot append raw slang arguments uses). This opts into Slang's existing
`IgnoreUninstantiatedModules` policy; syntax errors and semantic errors within the
selected hierarchy remain errors. Vendor RTL that hides simulation-only code between
`// synopsys translate_off` and `// synopsys translate_on` (the `synthesis` and `pragma` spellings too,
with or without a space after `//`) is excluded by default, matching synthesis semantics.
`--set compile.slang.translate_off=false` keeps that text for parsing and lowering instead.
The default enables slang's `--translate-off-format` for all three spellings; an unclosed
region is still an error. Explicit raw `--translate-off-format` arguments replace this list. Both are covered by `//inou/slang:slang_vendor_bundle_options`. The separate upstream argument
`--allow-use-before-declare` supports legacy declaration ordering. Ignoring uninstantiated modules and allowing use before declaration remain opt-in.
Unfinished reference RTL that must not even be parsed belongs inside a Verilog
`` `ifndef SYNTHESIS `` guard. Define it explicitly with `-- -DSYNTHESIS=1`.
The HighTide run uses both that define and `--ignore-uninstantiated-modules`.

`--set compile.specialize_modules=true` opts into the existing cross-instance
specializer before legalization. It folds constants per instance binding (up to 64 contexts per definition,
including a stateful definition), retains the original design roots,
and drops unused generic definitions. Loop legalization independently inlines
affected instances and defers their loop verdict until the existing cprop/bitwidth
cleanup, so a constant-configured router
does not require this optimization flag. Dynamic feedback still fails cycle
checking. The default remains false.

A `synth.liberty` RAM cell is bound as an opaque macro only when the instance
resolves to the injected Liberty declaration. A module of the same name in the
design sources (for example a behavioral SRAM model shipped next to the `.lib`)
fails the read: slang reports `DuplicateDefinition`, and with that diagnostic
silenced the reader still refuses it as `liberty-memory-name-collision` rather
than silently picking the macro pins or the RTL body. Remove the model from the
synthesis sources.

### Hard macros, vendor stubs and Yosys-style netlists (opt-ins)

These serve flows whose reference is Yosys/ORFS, where the Liberty/LEF view of a macro
replaces whatever the sources say about it. Each is `--set compile.slang.<flag>=true`
(or the value shown) and every one is off by default.

| flag | effect |
| --- | --- |
| `liberty_macro_precedence` | a `synth.liberty` RAM macro WINS over a same-named design module, like `read_liberty -overwrite`: the declarations go into a slang library of the highest priority, every instantiation of that name binds to the macro pins, and the shadowed source module (a vendor behavioral model, a wrapper that re-instantiates the macro under its own name) is never elaborated |
| `liberty_allow_truncation` | a wider actual on a narrower macro input keeps the low bits, as Verilog does, and `liberty-memory-port-truncation` becomes a warning instead of an error. The upper address/data bits are really discarded: use it only when the reference design does the same |
| `liberty_cells` | a module that no source defines but the selected Liberty does (a standard cell such as `BUFx2_ASAP7_75t_R` instantiated by a vendor-primitive stub) is bound as a typed, opaque cell instance with the Liberty's pins and directions, like a RAM macro. A source definition of the name always wins |
| `blackbox_attribute` | a module marked `(* blackbox *)` is its interface only: ports are declared, no body is built, and every instance stays an opaque `Sub` (the LEF/Liberty macro) instead of an empty module whose outputs read as unknown |
| `unconnected_input=zero\|one` | an instance input left open reads that constant instead of `x`. Yosys folds a vendor primitive whose controls are open (BUFG_GT's CE/CLR) into its enabled state; an unknown select keeps the whole enable cone as logic on a clock net |
| `flatten_state_arrays` | a one-dimensional unpacked state array without power-on contents is one packed register vector (constant selectors are element offsets, a runtime selector a shift/mask) instead of a Memory with one port per access site. NyuziProcessor's `pending_stores` has 375 read and 360 write ports, which otherwise take hours to bit-blast |

A port listed twice in a module header (`input a, input a`) is accepted by `--compat=vcs`
(`Redefinition` becomes a warning); the first declaration is the port, the later ones add
neither an IO nor an instance connection.

### Passive SystemVerilog interfaces

The direct reader lowers passive interface connections through the existing
LNAST tuple and module-call machinery. Named `input` and `output` modport members
become separate `<port>__in` and `<port>__out` bundles; no new graph node type is
needed. Supported members are uninitialized integral signals, including packed
arrays and packed structs, with value/type parameters. Interface instance arrays,
generated instances, forwarding through module ports, and registered writes to
output members are supported. Connected interface types participate in module
specialization; interface-bearing instances retain their own elaborated bodies.

Procedural or continuous logic inside an interface, unpacked signal arrays,
undirected interface ports, and modport `inout`, `ref`, expressions or methods
are rejected explicitly. This does not provide DPI or simulation file-operation
support. Combinational-loop checking remains enabled.

`//inou/slang:slang_passive_interfaces` compares emitted hardware against Yosys
`read_slang` for payload types, forwarding, array ordering, different connected
shapes and registered outputs, and checks unsupported-interface diagnostics.
