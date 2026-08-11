# pass.rocq

`pass.rocq` is the Rocq (formerly Coq) target for the LiveHD graph-to-theorem-prover
flow. It is the third prover stack after `pass.isabelle` and `pass.lean`, and it
is a separate pass with a separate proof stack — not a shared backend.

- Support library: `formal/rocq` (see its README for the semantic model).
- Literature review, and the reasoning behind every design choice here:
  [`LITERATURE_REVIEW.md`](LITERATURE_REVIEW.md).

## Toolchain

Rocq 9.x via opam:

```bash
opam update
opam install rocq-prover          # 9.2.0 at time of writing
eval "$(opam env)"
rocq --version
```

Known wrinkle on an `ocaml-system` switch: `rocq-core`'s dune split build cannot
find `rocq-runtime.kernel` unless `OCAMLPATH` points at the switch lib dir.

```bash
OCAMLPATH="$(opam var lib)" opam install rocq-prover
```

## Current implementation state

Emitted per design, into `--emit-dir rocq:DIR/`:

| File | Contents |
|---|---|
| `<Top>_Lgraph.v` | `Record <Top>_in / _out / _state`, and `<Top>_comb` / `<Top>_next` / `<Top>_step` as a topologically ordered `let`-chain over `BitVec w` |
| `<Top>_Lgraph_Cert.v` | `<Top>_nodeCerts`, `<Top>_sourceEnv`, `<Top>_graphCert`, `<Top>_outputsFromCert`, `<Top>_nextStateFromCert`, `<Top>_comb_cert` / `_next_cert` / `_step_cert`, the definitional `*_refines_cert` lemmas, and `<Top>_evalGraph_correct` instantiating the keystone |
| `_CoqProject` | `-Q . LiveHD` plus the emitted units (appended per design) |

**Two files, not one** — unlike `pass.lean`. Rocq compiles each `.v` to a cached
`.vo`, so iterating on the certificate does not re-elaborate the expensive
model. Lean has no such compilation unit, which is why its emitter produces a
single file and why "split the file" is still on its TODO.

Supported op set (same as `pass.lean`): `Nconst, Sum, Mult, Div, And, Or, Xor,
Ror, Not, LT, GT, EQ, SHL, SRA, Mux, Sext, Get_mask, Set_mask, Memory`.
Strict mode rejects `Latch, Fflop, Sub, LUT, AttrSet, Hotmux`, zero-width nodes,
X/Z constants, widths over `max_width`, and unsupported memory policies
(`ordering="none"`, and memories whose `bits` is not divisible by `wensize`).

Memories are modelled as function-valued state (`BitVec addr -> BitVec data`) with
`mem_read`/`mem_write`/`mem_write_be` and the SRAM policy combinators — never
scalarised into one flop per bit. Any number of read/write ports, async/array
(type 0/2) and sync-read (type 1), read-during-write forwarding, byte enables.

**Not implemented in this cut** (deliberate, mirrors what `pass.lean` deferred):

- the fast-view bridge `<Top>_comb = <Top>_comb_cert` as a *theorem* — the
  `GraphRefine` / `OpBridge` layer does not exist yet, so there is no
  `emit_fast_bridge` knob;
- the memory **certificate** — memory-bearing designs get the counts-only stub,
  same as `pass.isabelle` and `pass.lean`.

## Validation pipeline (order matters)

The LEC gate runs BEFORE generation. Steps 3-5 only claim *generated model =
LGraph certificate*; it is the LEC gate that ties the LGraph to the RTL.

```text
1. lhd compile         RTL -> LGraph
2. LEC gate            prove/classify RTL == LGraph    (scripts/run_dino_lgraph_lec_gate.sh)
                         accept: PROVEN, or INCONCLUSIVE (recorded)
                         reject: REFUTED -> do NOT generate
3. pass.rocq           LGraph -> model + certificate   (--emit-dir rocq:)
4. rocq typecheck      rocq makefile + make
5. certificate bridge  generated model = graph certificate
```

`scripts/run_dino_lgraph_rocq.sh` runs all five; `RUN_LEC_GATE=false` skips
step 2 for model-only bring-up, `RUN_ROCQ=false` skips step 4.

## Proof strategy

The same Strategy B as the other two stacks:

```text
generated fast Rocq model
  = evaluated graph certificate
  = mathematical graph-certificate semantics
```

The second link is `evalGraphCorrectForCert`, proved once and for all in
`formal/rocq/theories/Translation/LGraphModel.v`; every generated file just
instantiates it. The first link is the remaining bridge (see "Remaining work").

## Knobs

All under `--set formal.rocq.*` (and the shared `formal.strict` /
`formal.normalize`, which `formal.rocq.*` overrides).

| Knob | Default | Meaning |
|---|---|---|
| `strict` | `true` | abort on unsupported ops instead of emitting stubs |
| `normalize` | `true` | infer a zero-width `Get_mask` driver width from its consumers and write it back. **Implemented** here (it is a dead knob in `pass.lean`) |
| `emit_cert` | `true` | emit the certificate file at all; `false` gives a model-only scaling gate |
| `max_width` | `1024` | per-node width cap; `0`/`unlimited`/`inf`/`none` disables the cap. A proof-tractability guard, not a LiveHD limit |
| `cert_wf` | `skip` | `skip｜eval｜sorry｜chunked` — see below |
| `cert_wf_fallback` | `fail` | `fail｜sorry｜eval` for a `chunked` chunk containing an op with no `simpleOpCertWfBool` shape rule |
| `cert_chunk_size` | `25` | node certificates per chunk under `cert_wf=chunked` |
| `cert_chunk_limit` | `0` | emit only the first N chunks (0 = all). A dropped chunk is reported on stdout, never silent |
| **`eval_engine`** | `vm` | **Rocq-specific.** `vm｜native｜cbv` — which reducer closes a computational proof |

### `cert_wf` modes

- `skip` — a comment explaining how to turn it on.
- `eval` — one `graphCertWfBool ... = true` closed by the chosen engine.
- `sorry` — `graphCertWf` stated and `Admitted`. This is the *deliberate hole*
  mode; the run script's grep gate flags it.
- `chunked` — the certificate is split into `cert_chunk_size` pieces, each with
  its own `nodeCertChunkWfBool` lemma, plus a stronger
  `forallb simpleNodeCertShapeWfBool` lemma when every op in the chunk has a
  shape rule. Rocq re-checks the completed proof term at **every** `Qed`, so
  many small obligations beat one whole-graph proof.

### `eval_engine` and the trusted base

This axis has no counterpart in `pass.isabelle` or `pass.lean`, because Rocq is
the only one of the three where all three points are cleanly available:

| Setting | Tactic | Trusted base | Notes |
|---|---|---|---|
| `vm` (default) | `vm_compute` | kernel + bytecode VM | fast, and does *not* add a native compiler |
| `native` | `native_compute` | kernel + VM + the OCaml compiler and runtime | typically 2-5x faster than `vm` |
| `cbv` | `cbv` | kernel only | smallest TCB, slowest |

Compare: Isabelle's `eval` puts the code generator and ML compiler in the TCB
(`code_simp` is the kernel-only alternative); Lean's `native_decide` puts the
Lean compiler in the TCB (`decide` is the kernel alternative). Rocq's *default*
fast path is the middle option, which is why `vm` is the default here.

Caveat worth knowing: `vm_compute` and `native_compute` both **defy `Opaque`**.
Generated code must not rely on opacity for layering.

## Build

```bash
cd <livehd-new>
bazel build //pass/rocq:pass_rocq //lhd:lhd
make -C formal/rocq                       # the support library
bazel test //lhd/tests:lhd_rocq_emit_test # emitter contract (no Rocq needed)
```

`//lhd/tests:lhd_rocq_emit_test` asserts on the emitted *text* only, so it stays
green on a machine with no prover installed. Typechecking is the run script's
job.

## Smoke test

```bash
cd <livehd-new>
mkdir -p generated/pass_rocq_smoke/simple/{rocq,work}

./bazel-bin/lhd/lhd compile verilog generated/pass_rocq_smoke/simple_add.v \
  --reader yosys-verilog --top simple_add \
  --emit-dir rocq:generated/pass_rocq_smoke/simple/rocq \
  --workdir generated/pass_rocq_smoke/simple/work \
  --set formal.rocq.cert_wf=eval

cd generated/pass_rocq_smoke/simple/rocq
{ echo "-R <livehd-new>/formal/rocq/theories RocqSemanticPrimitives"; cat _CoqProject; } > p && mv p _CoqProject
rocq makefile -f _CoqProject -o Makefile.coq && make -f Makefile.coq
```

Both units typecheck with zero admits. The oracle checks (that the model
actually computes `a + b`, and that it agrees with the evaluated certificate on
concrete inputs) live in `generated/pass_rocq_smoke/simple/rocq/simple_add_Oracle.v`.

## DINO gate results (measured)

`scripts/run_dino_lgraph_rocq.sh` with `RUN_LEC_GATE=false JOBS=6`, Rocq 9.2,
`cert_wf=skip`, `max_width=1048576`. All three designs converted, typechecked,
and produced `.vo` with **zero** `admit`/`Admitted`.

| Design | node certs | model / cert lines | `.vo` size | rocq typecheck |
|---|---|---|---|---|
| `SingleCycleCPU` | 4772 | 9657 / 9299 | 585 KB / 1.1 MB | ~3 min |
| `PipelinedCPU` | 5061 | 10297 / 9892 | 672 KB / 1.2 MB | ~4 min |
| `PipelinedDualIssueCPU` | 10740 | 21721 / 20755 | 1.4 MB / 2.8 MB | ~22 min |

22m28s wall for all three (the designs are independent, so `make -j6`
overlapped them; `PipelinedDualIssueCPU` is the critical path). For comparison,
`pass.lean` reports ~3 min / ~6 GB for `SingleCycleCPU`, so the two stacks are
in the same ballpark at this size — but note Rocq re-checks only the file you
touched, which is what the model/certificate split buys.

`pass/rocq/scripts/op_census.py` on all three certificates: every `(op, arity)`
pair hits a real `denote_op` arm, every id list is `%N`, and every op has a
`simpleOpCertWfBool` shape rule (so `cert_wf=chunked` needs no fallback on
DINO). Op mix on `SingleCycleCPU`: 2093 `Op_GetMask`, 803 `Op_And`, 714
`Op_SRA`, 644 `Op_SHL`, 172 `Op_EQ`, 150 `Op_MuxN`, and n-ary `Op_Or` up to
arity 55.

Caveat: these runs skipped the LEC gate (step 2), which is the check that ties
the LGraph to the RTL. Re-run without `RUN_LEC_GATE=false` before treating any
of this as a statement about the *design*, rather than about the exporter.

## `scripts/op_census.py`

A seconds-cheap static gate to run **before** a long `rocq c`:

```bash
pass/rocq/scripts/op_census.py <path>/<Top>_Lgraph_Cert.v
```

1. **Arity.** Every arm of `denote_op` is total, so an unexpected arity does not
   get stuck — it silently denotes `mk_bv w 0`. That totality is required (a
   stuck Rocq reduction builds an enormous term), but it makes a wrong arity
   invisible. This is the check that catches it.
2. **Id scope.** Every id list must be `%N`. See the next section.
3. **Shape.** Which ops have a `simpleOpCertWfBool` rule, i.e. whether
   `cert_wf=chunked` will need a fallback.

Plus a width histogram and an all-ones-`Get_mask` count.

## Rocq-specific traps (read before editing the emitter)

1. **`nat` is unary.** A LiveHD node id of 2 000 000 000 as a `nat` literal
   builds a term with two billion successors the moment anything reduces it, and
   the evaluator compares ids on every step. All certificate ids are `N`
   (binary). Lean's `Nat` is a GMP bignum, which is exactly why `pass.lean` can
   ignore this — it is the sharpest divergence between the two ports.
2. **Scope delimiters do not propagate into list literals.** Generated files open
   `Z_scope`; a bare `[1; 2]` in an `nc_deps` field elaborates as `list Z` even
   though the field type is `list N`. Every emitted id list carries `%N`.
3. **`-Q` does not expose short names.** The `_CoqProject` binds the output dir
   with `-Q . LiveHD`, so the certificate must say
   `From LiveHD Require Import <Top>_Lgraph.`, not `Require Import <Top>_Lgraph.`
4. **Unbounded `Z.to_nat` is a memory bomb.** `Op_MuxN`'s selector and
   `Op_Sext`'s amount are guarded in `Z` before any conversion.
5. **Record projections are global functions.** Rocq has no `i.field` dot
   notation for a plain record, so an input reads as `(in_a i)`. Field names are
   uniquified across all three records by `make_field_name`, so the projections
   are globally unique inside a generated file.

## The one shared width resolver

The largest bug class in this pass family is the fast-model emitter and the
certificate emitter disagreeing on the width at which an operand is
materialised — 13 documented postmortems between `pass/isabelle/BRIDGE_BUGS.md`
and `pass/lean/STEP5_BRIDGE_BUGS.md`. Both of those passes compute the widths
twice, once per emitter.

`pass_rocq.cpp` has **one** `operand_dep_width(ctx, node, drivers, idx, out_w)`
that both `emit_node_expr` and `cert_node_expr` call. Every non-trivial rule
lives there:

- `LT`/`GT`/`EQ` operands at `max(operand pin widths)`;
- `SHL` port 0 at the node width, port 1 widened to hold a constant's value;
- `SRA`/`Sext` port 0 at its own pin width, port 1 widened likewise;
- `Get_mask` mask at `max(src_w, out_w)` — LiveHD's all-ones `-1` zero-extend
  idiom, which must not be materialised at the mask pin's declared 1-bit width.

Likewise there is exactly one constant spelling (`int_of_const`), used by both
the fast model and the certificate leaf, so a constant is textually identical on
both sides by construction.

**If you add an op, add its width rule to `operand_dep_width`, not to either
emitter.**

## Remaining implementation work

1. **Fast-view bridge.** `<Top>_comb = <Top>_comb_cert` as a theorem. Needs a
   Rocq `GraphRefine.v` (the `evalGraph_of_localAgree` local-recurrence lemma,
   ~176 lines in Lean, no Mathlib equivalent required) and an `OpBridge.v` of
   per-operator lemmas (~1000 lines in Lean, the expensive part). Lean measured
   ~280 ms/node on DINO, so plan for the file split from the start — which the
   two-file layout here already sets up.
2. **Memory certificates.** `Val = bv | mem`, `Op_MemRead`/`Op_MemWrite[BE]`, and
   the collision/read-first/write-first policy proofs. Currently a stub in all
   three passes.
3. **`graphCertWf` (the `Prop`) from `graphCertWfBool`.** The reflection lemma
   `graphCertWfBool cs srcs = true -> graphCertWf G` would let `cert_wf=eval`
   discharge the `Prop` rather than only the boolean, and would retire the
   `sorry` mode.
4. **De-duplicate the three passes.** `parse_memory_info`, `reachable_topo_order`,
   width resolution, field naming, and the certificate dep-id builder are now
   written three times (~600 lines each in `pass/isabelle`, `pass/lean`,
   `pass/rocq`). A shared `pass/formal_common` is the right refactor; it was
   deliberately out of scope here because the other two passes were being edited
   concurrently.
