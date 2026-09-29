# Rebasing Direction 2 onto PR #624

`masc-ucsc/livehd` PR #624 ("pass/lean: share verified and legacy certificate
pipelines", draft at the time of writing, head `3685a977e`) restructures
`pass/lean` into an owned graph scan plus a certificate IR, with one emitter per
direction. It lands the same decomposition as our Step 0 (`1d5b50127`), further
along. This file is what the rebase has to carry, measured rather than guessed.

## Free — the PR touches none of it

| ours | size |
|---|---|
| `pass/single_edge/` — all of Phase A and Phase B | +620/-89 |
| `graph/latch_contract.cpp` — nested-ICG flattening | +18 |
| `formal/lean/.../Compiler/Direct*.lean` — 8 files, Direction 2's whole Lean route | exclusive to us |
| most of `scripts/` | — |

## Clean patch — their Lean core IS our pre-Phase-B baseline

The PR refactored only C++. Every `Compiler/*.lean` file on `pr624` is
**byte-identical** to ours at `ea0492b37` (pre-Phase-B): `DesignCert.lean`
md5 `7a1fbce9`, and likewise `DesignSemantics`, `Runtime`, `CompileDesign`,
`ResidualIR`, `ResidualSemantics`. So Phase B's multi-clock type change
(`90583be13`, +197/-38 in `formal/lean`) applies directly.

`Translation/OpBridge.lean` differs on their side and not on ours -- take theirs.

## Delete — superseded by the PR

* `pass/lean/lean_common.{hpp,cpp}` and `pass/lean/IR_Semantics_pass.cpp`
  (+918/-688). Their `graph_access` + `design_scan` + `certificate_ir` +
  `emit_design_cert.cpp` is the same split, one layer finer, and with unit tests
  (`design_scan_test.cpp`, `certificate_ir_test.cpp`) we do not have.
* `pass/lean/design_cert_export.hpp`. NOT missing on their side -- reimplemented:
  our `DesignIn`/`SourceIn`/`FlopIn`/... become their `CertificateIR`/`Source`/
  `FlopDriver`/..., and our `class Remap` becomes their `index_certificate()`
  (same slot walk, same duplicate-id message, and it additionally checks
  dependencies are topological at that point).
* Most of the cycle guard in `ae2345d9f`. They fixed the same bug independently:
  `design_scan.cpp` keeps an `active` gray set and refuses on re-entry. They also
  hit the same dangling-reference trap and comment it ("Own the frame: growing
  stack invalidates references to stack.back()").

## Hand-port -- the actual work, ~280 lines of C++

1. **Phase B clock provenance** (`90583be13`, +231/-6 in `pass_lean.cpp`). Splits
   across their `design_scan.cpp` (the `latch_contract` clock walk, per-element
   root resolution, the gated-clock and falling-edge refusals) and their
   `emit_design_cert.cpp` (the clock table, ordinals on every flop/memory).
2. **The sync-ROM clock domain** (`2a948ced8`, +24/-2). A synchronous ROM has no
   `MemoryDesc` but its registered read port IS state, so it needs a domain or
   the lookup aborts. Their walk has the same shape and the same hole.
3. **The certificate's Phase B surface**, into `emit_design_cert.cpp`. Their
   emitter is the pre-Phase-B shape: zero mentions of `clocks`, `asyncReset` or
   `edges`, and `theorem X_step_correct : forall inp st`. Needs the
   `clocks := #[...]` array, `clock :=` / `asyncReset :=` on every flop line,
   `clock :=` on every memory line, and `forall edges inp st`.
4. **Additive parts of `ae2345d9f`**, which their fix does not include:
   * the cycle diagnostic naming BOTH endpoints and pointing at
     `split_selfref`'s `unresolved-cycle` warning (theirs says only
     "combinational cycle at node n_X"). On the nine CORE-ET modules this is the
     difference between a usable report and a name to go hunting with;
   * the traversal ceiling scaled to the graph (`64 * (nodes + 1024)`);
   * `formal.lean.max_nodes`.
5. **The `Emit_error` handler in `Pass_lean::work`** (~12 lines). THIS ONE MATTERS
   BEYOND US. Their `fatal()` (`graph_access.cpp:7`) throws `Emit_error` and
   there is no `catch` anywhere in their `pass_lean.cpp`, so every diagnosed
   refusal escapes to the kernel catch-all as `"class":"internal"` with
   `"errors":0` -- indistinguishable from a tool crash in the result JSON that
   the sweeps classify on. Worth upstreaming to the PR rather than keeping local.

## Sequencing advice

Do not rebase while the PR is a draft, but stop investing in the colliding area.
Of the remaining plan steps (`~/.claude/plans/zany-scribbling-goblet.md`):

* **safe now** -- 2 (core-et patch), 4 (`elab_top`), 6 (`split_selfref`),
  8 (L1 buffer arm), 9 (census): all in `scripts/`, `graph/`,
  `pass/single_edge/`, none of which the PR touches;
* **defer until after the merge** -- 5 (the ROM cap lives in
  `parse_memory_info`, which moves into their `graph_access`/`memory_lowering`),
  and the `pass/lean` half of 3 and 7.

Holding that line keeps the rebase at ~280 lines instead of letting it grow.
