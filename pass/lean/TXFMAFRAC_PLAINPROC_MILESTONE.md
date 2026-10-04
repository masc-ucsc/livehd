# txfmafrac_top accepted under a TARGETED plain-proc lowering

A module the default route cannot emit at all reaches direct-simulator
acceptance under an ALTERNATE lowering. This file is the durable record; the
logs it names are under `generated/` and are deliberately NOT staged.

**This is not a coverage change.** The generated ledger's route count stays
**109 of 122**. See "How to count this" below.

## Configuration -- this is the whole point, so it is stated first

| | |
|---|---|
| lowering | plain `proc` (candidate), NOT the shipped `proc -ifx` |
| how | `yosys.script` = `inou_yosys_read.ys` with `proc -ifx` -> `proc` |
| global default | UNCHANGED. `proc -ifx` remains the default for every other module |
| why | under `-ifx` this module has 39 combinational SCCs and pass.lean refuses; under plain `proc` it has 0 (`pass/lean/CYCLE_PROVENANCE.txt` part 6) |

## Artifact identities (full SHA-256)

Measured at commit `fc1d6a701`. Commits after it change only tests, BUILD,
this document, and one `friend` declaration in `inou/cgen/cgen_verilog.hpp`
that grants a test peer access to a private generator -- `cgen_verilog.cpp`,
`pass_single_edge.cpp` and every `ware/rtl` model are byte-identical to the
measured state (`git diff fc1d6a701 HEAD -- inou/cgen/cgen_verilog.cpp
pass/single_edge/pass_single_edge.cpp ware/rtl/` is empty).

| path | what | sha256 |
|---|---|---|
| `pass/single_edge/pass_single_edge.cpp` | tracked source | `ada3569d3d95ce995f09ce287b212dbc497736351a573305d42ead2ebce42954` |
| `inou/cgen/cgen_verilog.cpp` | tracked source | `ee13dae1cc0b6495a5ce28a82e8676030ecd8b9778e9c421164f9b999cd60b4d` |
| `ware/rtl/cgen_memory_1rd_1wr.v` | tracked support model | `e0c57724fbf66c60bbd3a086ce6ed902fdcf6716ee6ae3756b93570a10f94edb` |
| `generated/plainproc_txfmafrac/equiv7/read_plain_proc.ys` | candidate lowering script | `58103f928fb3be7f2bdb8a9740e11c9be15bd8478608074f2a196f1de4660786` |
| `generated/core-et/filelists/txfmafrac_top.f` | CORE-ET filelist | `e665f932f81dd2c6be73e907b70554074f6b7f853aff2936cb84178617de84e0` |
| `generated/plainproc_memfix/ds_norm/vectors.hex` | stimulus | `497807334ddc115b5cf1f24f186678a78bcaf0d173540eee6b45900b3941487e` |
| `generated/plainproc_memfix/raw.v` | raw netlist (recipe O0) | `9bd26b6fdd4adc238a9a60b72bcd6ddea050ea6c4d217fa748c34b9c52da90ef` |
| `generated/plainproc_memfix/norm.v` | normalized netlist (recipe O0) | `af8e7cb01da42a6480f7c195d3f2a94867ab919e9d0f20cb4492f7230358db7e` |
| `generated/plainproc_memfix/lean/txfmafrac_top_Lgraph.lean` | certificate | `e87dc22c7c8e0d3efd2e183288f5c3817808bf6e8da9e4341da9d4e54728754f` |
| `generated/plainproc_memfix/lean/txfmafrac_top_io.json` | certificate IO map | `23718771ef0b80621e1dbbdf17219a24151a9730ea11daa9112d386ce8eaefd9` |

`lhd` binary as measured (k8-dbg):
`ee2e7824e8e0aa6b4c73a05e5edde2825d8373728c30734fb5edcf750d51c8f8`
at `bazel-out/k8-dbg/bin/lhd/lhd`. That path is a build output, not a tracked
artifact, so it cannot be re-verified from the repository alone; it is
recorded as measured.

core-et revision `c72b6a2fe4aaad5a9eef437776864ff71f4b55d7`
(`/soe/czeng14/projects/core-et`), which had 1 dirty file at measurement time
-- so the RTL input is identified by the filelist and per-file hashes above
rather than by that revision alone.

### Graph identities -- full content manifests

`library.txt`+`srcmap.txt` alone is only a PARTIAL identity: it omits
`body.bin` and `overflow.bin`, which hold the graph itself. Sorted full
manifests (hashed read-only; the graphs were not altered):

INPUT graph, preserved pre-single_edge (`generated/plainproc_txfmafrac/equiv7/txfmafrac_top/lgdb_raw`):

```
    b84b5a8336baecb4a3645c672f5c001c240488388fbc297f5314cd40843d51cb  generated/plainproc_txfmafrac/equiv7/txfmafrac_top/lgdb_raw/graph_930469305945/body.bin
    1269d2bb74dc150155dea0b723820ca75a3a46156ed57472e4be88d1e3e3faa4  generated/plainproc_txfmafrac/equiv7/txfmafrac_top/lgdb_raw/graph_930469305945/overflow.bin
    e7cd5f1b6e997249a25cec4ea99971f785831c6fb63f7c5274c6d9abf39701d9  generated/plainproc_txfmafrac/equiv7/txfmafrac_top/lgdb_raw/library.txt
    e7e89de9ab43800b8ffe100e0848f84bbd829ecd17dd14489fe93fab5a439d74  generated/plainproc_txfmafrac/equiv7/txfmafrac_top/lgdb_raw/srcmap.txt
```

OUTPUT graph, freshly normalized (`generated/plainproc_memfix/lgdb_norm`):

```
    8042a177ac41a7d0d23c7d6d90ad76ab2a9a0aa4230ba98cf3a365c54d15c9e6  generated/plainproc_memfix/lgdb_norm/graph_930469305945/body.bin
    de89b373ceb14988641c854812bc9f8a0c94e0dccd00970fc5e7cac9ff037a6e  generated/plainproc_memfix/lgdb_norm/graph_930469305945/overflow.bin
    e7cd5f1b6e997249a25cec4ea99971f785831c6fb63f7c5274c6d9abf39701d9  generated/plainproc_memfix/lgdb_norm/library.txt
    b124424a52e59c12f08a11cc1f8a87e77704de2f44206d2e2f80341f484b8156  generated/plainproc_memfix/lgdb_norm/srcmap.txt
```

## Recipes

* raw and normalized emission: `--recipe O0`, which resolves to exactly
  `inou.cgen.verilog` -- no cprop, no `pass.formal`, no normalization.
* normalization: `lhd pass single_edge` (P=1, 13 latches retyped, 140 gates
  folded into enables, 1 clock domain).
* certificate: `formal.lean.mode=verified_compiler`, `strict=true`.

## Gates

| gate | result |
|---|---|
| raw vs ORIGINAL RTL, 3142 vectors (seed 1) | **PASS** 0 mismatches |
| normalized vs ORIGINAL RTL, 3142 vectors | **PASS** 0 mismatches |
| certificate emit | PASS, 3.7 MB |
| Lean typecheck / compiles theorem | PASS, 1032.7 s, 10.06 GiB |
| checkDesign | PASS, 1709 ms |
| 4x runDirect | PASS, 4 cycles, 4185 ms |

`direct_sweep` row: ACCEPTED, sources 24100, nodes 28850, outputs 78,
flops 153, mems 0, wall 1086.0 s, rss 4.58 GiB.
Axioms: `propext, Classical.choice, Quot.sound,
txfmafrac_top_compiles._native.native_decide.ax_1_1`.

Both differentials were rerun after the support-model change, so the raw
control is not stale. Every run under `MemoryMax=20G`, `MemorySwapMax=0`;
zero OOM kills.

## Two defects this exposed, with their ACTUAL locations

They are in different components and should not be described as one.

1. **`pass/single_edge`** -- the ICG enable-latch bypass tested only the
   enable pin's immediate master node, so an enable reaching the gate through
   a `Get_mask` zext wrapper kept the latch's Q, which is a cycle stale once
   the latch is retyped. Commit `535a7abd2`. This one IS a normalization
   defect. 3141 -> 473 mismatches.
2. **the cgen memory support model and its generated template** -- a
   registered read updated unconditionally from a read that is x while
   disabled, so a disabled cycle loaded 0 instead of holding. Commit
   `fc1d6a701`. This is NOT a normalization defect: it is the Verilog support
   model disagreeing with the contract the import, pass.lean and
   pass.single_edge already share. **pass.lean's read semantics were already
   correct** (`sram_sync_read_reg_next ren raw cur = if ren then raw else cur`)
   and are not called into question by it. 473 -> 0 mismatches.

## What these gates do NOT establish

* A 3142-vector differential is FINITE-VECTOR evidence on DEFINED inputs. It
  is not proof of RTL equivalence, and the domain deliberately excludes the
  X-semantics question that `-ifx` versus plain `proc` is actually about.
* The differentials validate the EMITTED VERILOG; the Lean gates validate that
  the CERTIFICATE compiles, checks and runs. Two artifacts from one graph --
  **certificate acceptance is not RTL agreement**, and nothing here closes
  that gap.
* `mems=0`: all 17 memories are ROMs carried as ROM sources, so the
  certificate's memory-WRITE path was not exercised.
* Defect 1 was a normalization defect, so for blocks already accepted on the
  default route a passing checkDesign/runDirect does not establish RTL
  agreement -- those gates run on the normalized graph. A block with a gated
  synchronous read was exposed to defect 2 as well.

## How to count this

The ledger's **109 of 122** is the count on the DEFAULT route and is unchanged.
txfmafrac_top is accepted only under the targeted plain-proc configuration
above.

Counting the union: **110 distinct CORE-ET blocks have direct-simulator
acceptance when this alternate lowering is included**, the 110th being
txfmafrac_top under a non-default lowering, gated on a finite-vector
differential rather than on the default route.

`pass/lean/COVERAGE_LEDGER.md` is GENERATED by `scripts/coverage_ledger.py`
and must not be hand-edited. Any later integration has to represent the route
distinction explicitly -- a row copied into an ordinary census directory would
read as a default-route acceptance, which this is not.

## Logs (generated/, ignored, not staged)

```
generated/plainproc_memfix/           GATE_TABLE.txt env.txt ds_raw.log ds_norm.log
                                      typecheck.log direct raw.v norm.v lean/ lgdb_norm/
generated/plainproc_txfmafrac/        the original run and the preserved lgdb_raw
generated/plainproc_fixed/            the 473-mismatch intermediate
generated/plainproc_txfmafrac_retry/  the 3141-mismatch starting point
generated/diag209, generated/diag320  the two diagnosis rounds
```
