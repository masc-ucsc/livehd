# Legacy pass: shared architecture (L0–L8)

Baseline: `1af4e1492` on `b1-b2-verified-compiler`. This change implements the
legacy work packages in `PASS_LEAN_RESTRUCTURE_PLAN.md`; it does not integrate
D2, D4, or other research branches.

## Ownership and entry points

Both EPRP modes now run `scan_design` and `build_certificate` before dispatch.
There is one graph traversal, operation mapping, memory lowering, and synthetic
ID allocator. The legacy public entry point is:

```cpp
void emit_legacy_model(const DesignScan&, const CertificateIR&,
                       const LegacyEmitOptions&, std::ostream&);
```

The emitter libraries do not link HHDS or EPRP. `legacy_model.cpp` is 48 lines
of orchestration. `legacy_support.hpp`, its private certificate structures, and
its graph traversal have been deleted.

| Library | Responsibility |
|---|---|
| `lean_legacy_schema` | Named ports/state, globally unique selectors, RTL mapping comments |
| `lean_legacy_fast_model` | Pure `lower_fast_expr(CertificateIR, CertNode)` and simultaneous next state |
| `lean_legacy_graph_cert` | Sparse-ID GraphCert, source environment, output/state projections |
| `lean_legacy_cert_wf` | Optional chunk proofs and dense-slot uniqueness checks |
| `lean_legacy_bridge` | Optional per-node operator lemma instantiations |
| `lean_legacy_emitter` | Coordinate the preceding libraries |

Only `pass_lean` is `alwayslink`. The scan now preserves raw state names and
output declaration identity. Flop/memory drivers retain origin and read-port
provenance, so naming never reconstructs memory policy or allocates semantic IDs.
Shared `lean_format` owns literals, lists/arrays, lookup trees, operation spelling,
and atomic file writes. Exceptions preserve the previous complete output and
remove the temporary file.

## Intentional changes at integration

The formatting-only commit is byte-identical on all saved accepted fixtures.
The final integration is not a move-only change:

- Legacy accepts the same graphs as the verified exporter. ROM contents are
  constants, synchronous ROM read registers remain state, and reset value,
  polarity, and asynchronous Q reads follow the shared certificate.
- Legacy fast expressions now use certificate operand widths and dependencies.
  Narrow division divides before truncation; signed comparisons interpret each
  operand at its own width; Sext honors its sign-position operand. Widening SRA
  preserves its sign. Focused Lean oracles cover these cases.
- Memory lets and next-state bindings use the canonical lowered node IDs. State
  updates all read old state, including a two-flop swap regression.
- Legacy formatting changes include the header, parentheses, explicit reset
  literals, memory let chains, and RTL mapping comments. Public model names and
  the existing certificate/refinement theorem names remain available.
- Unsupported optional bridge shapes fail atomically instead of inserting
  `sorry`. General/dynamic Sext remains executable, but its optional bridge
  requires the existing full/low sign-position lemma shape.
- `cert_wf=chunked` now emits actual proofs; the baseline only emitted a pending
  comment. Constants use a symbolic constant-chunk lemma. Mixed chunks separate
  local shape checks from concrete dependency-subset checks. Global uniqueness
  maps sparse IDs to the canonical dense slots and proves injectivity via a
  range, avoiding a giant `distinct all_ids` evaluation. Unsupported shapes fail
  by default. `eval`/the explicit `eval` fallback use local concrete checks;
  `sorry` requires an explicit option and is visible in the emitted axiom audit.
  A chunk limit emits partial facts and never claims whole-graph WF.

Graph acceptance and optional proof-generator coverage are separate. For example,
Mult is executable but has no selected legacy operator bridge, and UDiv/SetMask
are intentionally unsupported by the baseline B1/B2 residual compiler. Their
legacy oracle checks use `interpretDesign` and assert that compiler refusal;
this refactor does not expand the verified compiler's operation set.

## Validation

`bazel build -c dbg //pass/lean:pass_lean //pass/lean:lean_export_graph` and
`bazel test -c dbg //pass/lean:lean_export_tests` pass (three C++ test binaries).

| Saved corpus | Legacy baseline accepted | Shared legacy accepted | Verified accepted |
|---|---:|---:|---:|
| DINO | 3 / 3 | 3 / 3 | 3 / 3 |
| CORE-ET | 56 / 94 | 85 / 94 | 85 / 94 |
| CVA6 | 15 / 51 | 46 / 51 | 46 / 51 |

- All 134 accepted verified artifacts are byte-identical to `1af4e1492`.
- All 74 previously accepted legacy corpus artifacts and 22 accepted baseline
  tiny fixtures preserve parsed NodeCert values, source IDs/expressions, topo
  order, output projections, and next-state dependency IDs/types. This is a
  structural comparison, not a claim that arbitrary Lean text was proved equal.
- 27 accepted saved tiny fixtures pass Lean checks comparing legacy fast output
  and next state with GraphCert and DesignCert evaluation. The other 11 fixtures
  retain explicit refusals (unsupported operators, clock/depth policies,
  initialized RAM, whole-array memory, undefined/unknown forwarding, malformed
  memory policy, and unsupported initialization forms).
- Five additional owned-data oracles pass: narrow division, dynamic Sext,
  unequal-width signed comparison, widening SRA, and simultaneous flop swap.
- All 24 supported tiny mixed-chunk WF artifacts and one explicit constant-only
  chunk pass Lean and their axiom audits. Three unsupported simple shapes refuse
  by default. Dense uniqueness and multi-chunk composition are exercised by RAM.
- All 23 supported tiny optional bridge artifacts pass Lean and axiom audits,
  including RAM, byte enables, forwarding, ROM, sync reads, and reset variants.
  Mult, UDiv, SetMask and the noncanonical Sext fixture refuse explicitly.
- The three DINO `compileDesign_correct` instantiations pass with no `sorryAx`.
  `native_decide` axioms remain explicit; no claim of eliminating that trust is made.
- The remaining 14 saved corpus graphs refuse on combinational cycles in both
  modes. There is no silent fallback.

No RTL frontend or LEC run is claimed here. These gates re-export saved graph
DBs. Upstream RTL-to-LGraph LEC remains a separate validation gate.

## Reproduction and artifacts

Runtime artifacts belong under `generated/legacy_refactor/` in the worktree.
The original dirty workspace is only a source of read-only graph copies.

- `tests/legacy_fixtures.cpp` / `lean_legacy_fixtures`: saved tiny graph generator.
- `tests/compare_exports.py`: copies saved graphs before opening, records hashes,
  acceptance/refusals, and certificate summaries. Manifest rows contain
  `group`, `top`, and an absolute `graph_dir`.
- `tests/audit_legacy_parity.py BEFORE AFTER REPORT.json`: structural comparison.
- `tests/check_legacy_oracles.py LEGACY VERIFIED RUNTIME ENV.json`: Lean checks
  for tiny fixtures. `ENV.json` supplies the Lean executable search path and
  freshly built library `LEAN_PATH`; no network or RTL elaboration is required.
- `LEAN_LEGACY_OUTPUT=<project-local-path> legacy_model_test` writes the owned
  arithmetic/state and constant-chunk oracle artifacts.
- `tests/LEGACY_REFACTOR_GOLDENS.tsv`: baseline hashes.
- `tests/LEGACY_SHARED_RESULTS.tsv`: final corpus hashes and counts.

Build `LeanSemanticPrimitives.Translation.LegacyCertWF` with the support package
before elaborating generated `cert_wf` artifacts. The standalone exporter accepts
optional `LABEL=VALUE` arguments after its mode, including `emit_fast_bridge`,
`cert_wf`, `cert_chunk_size`, and `cert_chunk_limit`.
