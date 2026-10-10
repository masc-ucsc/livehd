# B1 verified-compiler refactor

This change applies the B1 assignment in `PASS_LEAN_RESTRUCTURE_PLAN.md`
(line 218) to `b1-b2-verified-compiler`, starting at `b04288cca`. It does not
integrate Direction 2, Direction 4, or change the Lean DesignCert schema.

The verified path is now:

```text
Pass_lean / LeanOptions
  -> scan_design(Graph, ScanOptions) : DesignScan
  -> build_certificate(DesignScan, CertificateOptions) : CertificateIR
  -> emit_design_cert(DesignScan, CertificateIR, ostream)
```

- `graph_access.*` contains the existing HHDS access, width checks, and memory
  policy parser. These helpers remain shared with compatibility mode.
- `design_scan.*` owns port identities, decimal constants, flop controls,
  resolved async-reset input provenance, memory policies and contents, clock
  connections, roots, and reachable topological nodes. No HHDS handle escapes.
  Declaration order determines roots; sorted names determine runtime input and
  output ordinals. Changing either order changes the artifact.
- `certificate_ir.*` builds typed operations, sources, dependencies and state
  drivers. Sparse identities remain available alongside validated dense slots.
- `memory_lowering.*` creates per-read forwarding chains, byte-enable writes,
  immutable ROM images, and synchronous read registers. It never reads a graph.
- `lean_format.*` and `emit_design_cert.*` contain the Lean syntax and checked
  atomic file output. They link without HHDS or the pass registry. Integer data
  stays decimal until formatting; the implicit enable on a synchronous raw read
  retains its historical literal spelling.
- `lean_options.*` parses the existing labels. `pass_lean.cpp` is a 65-line
  adapter and dispatcher. Only `:pass_lean` requires `alwayslink` among these
  libraries.

The old model and bridge implementation is relocated to `legacy_model.cpp`
with its private certificate structures in `legacy_support.hpp`. Its algorithms
are not being redesigned. The verified path no longer calls that implementation
or constructs its text fragments. `verified-compiler.*` and the header-only
`design_cert_export.hpp` are removed.

The B1 single-edge precondition is unchanged: upstream normalization is still
responsible for clock domains. Clock connections are recorded for future
consumers, but this does not introduce Direction 2 edge-vector semantics.
ROM classification, async reset provenance, constant-width rules, source
allocation order, memory forwarding, and synchronous read enables retain B1
behavior. The scan detects combinational cycles instead of growing an unbounded
DFS stack.

## Validation

Build and run the public-library tests:

```sh
bazel build //pass/lean:pass_lean //pass/lean:lean_export_graph
bazel test //pass/lean:lean_export_tests
```

The tests exercise owned scan lifetime, root and port ordering, cycle and
zero-width refusals, constant/shift/compare/GetMask/Sext/SetMask widths, mux
polarity, flop controls, memory forwarding and byte enables, synchronous ROM
state, dense identities, and atomic output failure.

`lean_export_graph LGDB TOP OUTPUT_DIR` re-exports a saved normalized graph,
without rerunning RTL elaboration. `scripts/compare_exports.py` compares two
versions of this tool on **copies** of saved databases. Its JSON manifest lists
`group`, `top`, and `graph_dir` relative to `--graph-root`. It records exit codes,
SHA-256 hashes and semantic certificate counts; a timeout or missing output is
never counted as a passing certificate.

All runtime data for this refactor is under `generated/b1_refactor/` in the
new worktree. The source checkout is read only. A baseline source snapshot and
exporter built from `b04288cca` live there as well. Full-driver validation was
initially blocked by the pinned Yosys-slang archive checksum; the standalone
export tool avoids that unrelated dependency and compares exactly the same
post-normalization graphs.

The Lean compiler modules are rebuilt from this checkout against the existing
Lean 4.31.0 / mathlib dependency cache, with all new `.olean` files and compiler
temporary files inside this worktree. The generic compiler axiom audit and the
three DINO certificate proofs are separate checks. These checks establish no new
claim about RTL elaboration or upstream LEC.

### Recorded run (2026-09-26)

The saved corpus contains 148 graph databases. The successful exports were
byte-identical to `b04288cca`: **3 DINO, 85 CORE-ET, and 46 CVA6**. The remaining
14 databases contain combinational cycles: the baseline exceeded a 15-second
export cap, while the new scanner refused with a cycle diagnostic. Two of those
baseline hangs were also observed with a 180-second cap. These partial upstream
artifacts are not counted as accepted designs or Lean proofs.

`tests/B1_REFACTOR_GOLDENS.tsv` records baseline hashes and semantic counts for
the refactor, including mutable memory and ROM examples. The saved corpus is
larger than the historical checked-in sweep; those historical verdict files and
the wrapper generators are unchanged. A legacy relocation smoke export was
also byte-identical (`txfma_c3`).

All **13 C++ tests** pass. The compiler modules rebuild; `compileDesign_correct`
and `compileAndRun_correct` use only `propext`, `Classical.choice`, and
`Quot.sound`. All three generated DINO certificates elaborate and discharge
`_step_correct`, with no `sorryAx` (their existing `native_decide` acceptance
checks remain part of the trust boundary). Measured DINO elaboration times were
42.9 s, 50.5 s, and 108.2 s, respectively.

### Separate correction: active-low flop next state

After committing the byte-preserving refactor as `afafd4f70`, fix the existing
B1 reset-polarity omission separately. The old exporter set
`FlopDesc.resetActiveLow` by consulting `flop_negreset`, a map that the scanner
never filled. The actual polarity was recorded in `flop_active_low` and used
only for `SourceDesc.flopQAsync`. Consequently an asserted active-low reset
changed the combinational read but the next-state rule treated that same reset
as active-high.

The certificate builder now carries `Flop.active_low` to both descriptors.
The new graph test follows a primary reset through a resize node and checks the
resulting source and flop. Its optional Lean oracle checks immediate reset,
reset priority over a disabled enable, holding state with reset released, and
taking `din` when enabled. Both the C++ assertion and the Lean next-state checks
fail before the correction; all pass afterward. No Lean semantics or compiler
proof is changed. The final suite has **14 passing C++ tests**.

All 134 accepted corpus graphs were regenerated after the fix. The only text
changes are `resetActiveLow := false` becoming `true` in 3626 flop descriptors
across 46 designs. All three DINO files remain byte-identical to the already
proved versions. `tests/B1_RESET_POLARITY.tsv` records the changed files and
final hashes; unchanged files retain the hashes in `B1_REFACTOR_GOLDENS.tsv`.

To reproduce the small Lean oracle after building the C++ test:

```sh
mkdir -p generated/b1_refactor/reset_check
LEAN_RESET_FIXTURE="$PWD/generated/b1_refactor/reset_check/reset.lean" \
  bazel-bin/pass/lean/design_scan_test \
  --gtest_filter=DesignScan.ActiveLowAsyncResetSurvivesResizingAndControlsNextState
cd formal/lean
lake env lean ../../generated/b1_refactor/reset_check/reset.lean
```
