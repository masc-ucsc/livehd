# CVA6 → livehd-new → pass.isabelle without Bender and without gate wrappers

**Audience:** anyone bringing CVA6 RTL into `livehd-new` / `pass.isabelle`.

**What this replaces:**

1. **Bender at run time** → use the *static* filelists already captured in
   `livehd-new/generated/cva6_filelists/*.flistplus.f`.
2. **Hand-written gate wrappers** (`scripts/cva6_module_wrappers/*_gate.sv`) →
   use **sv2v** to monomorphize CVA6's parameters/structs, exactly the way the
   DinoCPU flow is driven through the *verilog* front-end.

Both changes point the CVA6 flow at the same path Dino already uses: the
`yosys-verilog` reader (`read_verilog -sv` → `yosys2lg` → `pass.cprop` →
`pass.bitwidth` → `pass.isabelle`). The only thing standing between CVA6 and
that path is that CVA6 is heavy SystemVerilog (config-struct parameters,
`type` parameters, packages, interfaces, `generate`). **sv2v lowers all of
that to plain Verilog** so `read_verilog -sv` can ingest it.

---

## 0. The two front-ends (why this works)

`lhd compile verilog --reader <R>` accepts three readers
(`lhd/lhd_options.cpp:305`, `lhd/lhd_meta.cpp:65`):

| `--reader` | yosys command | Notes |
|---|---|---|
| `yosys-slang` | `read_slang` (whole-program SV elaboration) | What CVA6 currently uses. Full SV semantics, but this is where the CVA6 runs fail (index-oob, endianness, and a downstream `pass/cprop/cprop.cpp:459` assertion). |
| `yosys-verilog` | `read_verilog -sv -nomeminit` per file | **What Dino uses.** Yosys's own (weaker) SV parser; needs already-lowered sources. |
| `slang` | `inou.slang` SV→LNAST | Pyrope flow, stops at LNAST. Not for us. |

How Dino is actually driven (`generated/dino_lgraph_isabelle_livehd_new_20260605/logs/PipelinedCPU.direct.log`):

```
inou.yosys.tolg files:ALU.sv,ALUControl.sv,...,PipelinedCPU.sv top:PipelinedCPU
  |> pass.cprop |> pass.bitwidth |> pass.cprop |> pass.isabelle
# inside yosys (inou_yosys_read.ys), frontend=verilog:
verilog_defines -DSYNTHESIS=1
read_verilog -sv -nomeminit <each file>
hierarchy -top PipelinedCPU
flatten
chformal -remove
proc; opt_expr; opt_merge; pmuxtree; memory -nomap
write_verilog pp.v
yosys2lg -path .../lgdb
```

Dino needs **no sv2v** because firtool already emits simple SV. CVA6 does not —
so we insert **sv2v** as the lowering step that produces Dino-grade Verilog.

```
                bender (once, offline)            sv2v (this doc)            Dino-style verilog front-end
CVA6 sources ───────────────────────► static .flistplus.f ───────────────► cva6*.lowered.v ──► lhd --reader yosys-verilog ──► pass.isabelle
              (deps resolved into                (monomorphize params,                          (read_verilog -sv → yosys2lg)
               .bender/git/checkouts)             flatten structs/generate)
```

---

## 1. Static filelists instead of Bender

### 1.1 What is already captured

`livehd-new/generated/cva6_filelists/` holds frozen Bender `flist-plus`
snapshots. For the SV39 config use:

```
generated/cva6_filelists/cv64a6_imafdc_sv39_hpdcache_wb.top_cva6.flistplus.f   # 220 lines, top trimmed to `cva6`
```

> **Use SV39, not SV32.** `cv32a6_imac_sv32` fails in the front-end on
> Sv32-only constructs — `cva6_tlb.sv:322` references `vpn[2]` (Sv32 has only
> 2 VPN levels → index-oob) and `cva6_ptw.sv:528` `pte.ppn[PPNW-1:GPPNW]`
> (descending/ascending endianness mismatch). SV39 has 3 VPN levels and clean
> slices, so it gets past elaboration.

### 1.2 Filelist format

It is a yosys/Bender `flist-plus`: three line kinds, in dependency order.

```
+incdir+/.../include              # include search dir
+define+TARGET_CV64A6_..._WB      # preprocessor define
+define+TARGET_FLIST
/abs/path/to/source.sv            # a source file (already dependency-sorted)
```

The source paths point into `cva6/.bender/git/checkouts/...`. **That is the
whole point of "instead of bender":** Bender was run *once* to populate
`.bender/git/checkouts/` and to emit this file; from now on you reuse the
static file and never invoke Bender again. (To refresh after a dependency
bump, regenerate with
`bender script flist-plus -t cv64a6_imafdc_sv39_hpdcache_wb --top cva6 --trim-incdirs auto > <file>`
— but that is the only time Bender is needed.)

### 1.3 Feeding the static filelist to the existing runners

Both stress runners already accept `CVA6_FILELIST=` and copy it verbatim,
skipping the Bender branch (`run_cva6_module_isabelle_stress.sh:90`,
`run_cva6_complex_isabelle_stress.sh:125`):

```bash
CVA6_FILELIST=$PWD/generated/cva6_filelists/cv64a6_imafdc_sv39_hpdcache_wb.top_cva6.flistplus.f \
CVA6_TARGET=cv64a6_imafdc_sv39_hpdcache_wb \
  scripts/run_cva6_complex_isabelle_stress.sh
```

That path still uses `--reader yosys-slang`. The rest of this document is the
**sv2v / verilog-frontend** alternative.

---

## 2. sv2v instead of gate wrappers

### 2.1 Why wrappers existed, and why sv2v removes them

The gate wrappers (`cva6_tlb_gate.sv`, `tc_sram_gate.sv`, …) exist to **bind a
module's parameters and `type` parameters to concrete values** so a leaf
module can be elaborated in isolation — e.g. `cva6_tlb` is
`#(parameter type pte_cva6_t = logic, …)`, useless until something supplies the
real struct types from `CVA6Cfg`.

sv2v does this binding by **monomorphization**: given a top whose parameters are
fully bound (the whole `cva6` core, or any module instantiated with concrete
params), sv2v specializes every parameter, expands `CVA6Cfg`/`build_config()`,
flattens packed structs to bit-vectors, unrolls `generate`, and drops packages
and interfaces. The result is plain Verilog with **no parameters and no struct
types left to bind** — so no wrapper is needed to declare them.

### 2.2 sv2v on this machine

```
/mada/software/verilog/sv2v        # v0.0.12-19-g7808819
```

Relevant flags (`sv2v --help`):

| flag | meaning |
|---|---|
| `-I DIR` | include search dir (≙ `+incdir+`) |
| `-D NAME[=VAL]` | define a macro (≙ `+define+`) |
| `--top=NAME` | keep only NAME's cone; **prunes uninstantiated modules** |
| `-w FILE.v` | write a single lowered `.v` |
| `-E CONV` | exclude a conversion (`Assert`, `Always`, …) |
| `--bugpoint=SUBSTR` | shrink input to the smallest piece reproducing an error |

> sv2v has **no `-f filelist` option.** You must translate the flist's
> `+incdir+`/`+define+`/path lines into `-I`/`-D`/positional args (snippet
> below).

### 2.3 Translate a `.flistplus.f` into sv2v args

```bash
flist2sv2v() {  # usage: flist2sv2v file.flistplus.f > sv2v.args  (one arg per line)
  while IFS= read -r line; do
    [ -z "$line" ] && continue
    case "$line" in
      //*)        : ;;                                  # comment
      +incdir+*)  printf -- '-I\n%s\n' "${line#+incdir+}" ;;
      +define+*)  printf -- '-D\n%s\n' "${line#+define+}" ;;
      *)          printf -- '%s\n' "$line" ;;           # source path
    esac
  done < "$1"
}
```

Load into an array and call sv2v:

```bash
mapfile -t SV2V_ARGS < <(flist2sv2v cv64a6_imafdc_sv39_hpdcache_wb.top_cva6.flistplus.f)
/mada/software/verilog/sv2v --top=cva6 -D VERILATOR "${SV2V_ARGS[@]}" -w cva6.lowered.v
```

### 2.4 Two defines you will need

* **`-D VERILATOR`** — CVA6's instruction tracer (`common/local/util/ex_trace_item.svh`)
  is a SystemVerilog **class** guarded by `` `ifndef VERILATOR ``. sv2v cannot
  parse classes (`Parse error: unexpected token 'new'`). Defining `VERILATOR`
  skips all tool-only tracer code. (Dino's analogue is `-DSYNTHESIS=1`.)
* The `+define+TARGET_…` lines from the flist are already carried through by
  the translator above; keep them.

---

## 3. The verified path (what actually runs today)

### 3.1 Whole-core lowering: blocked by the CV-X-IF accelerator

```bash
sv2v --top=cva6 -D VERILATOR "${SV2V_ARGS[@]}" -w cva6.lowered.v
# => sv2v: field 'acc_req' not found in struct ... acc_dispatcher_*.acc_req_t_CVA6Cfg[...]
```

`cv64a6_imafdc_sv39_hpdcache_wb` sets `CVA6ConfigCvxifEn = 1`
(`core/include/cv64a6_imafdc_sv39_hpdcache_wb_config_pkg.sv:29`), so
`acc_dispatcher.sv` + `cvxif_*.sv` are **active** and cannot simply be dropped.
sv2v's struct-field resolver chokes on the accelerator's config-function-derived
packed structs. **=> A clean whole-core sv2v lowering of this config is not
currently reachable.** (The slang front-end does get past elaboration here, only
to die later at `pass/cprop/cprop.cpp:459 :assertion inp_edges_ordered.size() > 1`
— a livehd-new EQ-fold bug. So *both* front-ends are blocked on the full core,
in different places.)

### 3.2 Subtree lowering: works, and prunes the accelerator away

Because `--top=<module>` removes everything outside that module's cone, you can
lower any subtree that does **not** pull in the accelerator. The MMU TLB is the
worked example. It is still instantiated through a one-line top
(`scripts/cva6_module_wrappers/cva6_tlb_gate.sv`) whose only job is to bind
`CVA6Cfg`/`TLB_ENTRIES`/`HYP_EXT` — sv2v then erases all the SV complexity, so
you do **not** hand-write any struct typedefs or port tie-offs beyond that
instantiation.

```bash
OUT=generated/cva6_sv2v_lower_sv39
mapfile -t SV2V_ARGS < <(flist2sv2v generated/cva6_filelists/cv64a6_imafdc_sv39_hpdcache_wb.top_cva6.flistplus.f)
SV2V_ARGS+=("scripts/cva6_module_wrappers/cva6_tlb_gate.sv")

/mada/software/verilog/sv2v --top=cva6_tlb_gate -D VERILATOR "${SV2V_ARGS[@]}" \
  -w "$OUT/cva6_tlb_gate.lowered.v"
# => exit 0, 749 lines, 2 modules (cva6_tlb_gate + monomorphized cva6_tlb), 0 warnings
```

### 3.3 End-to-end into pass.isabelle (Dino-style)

```bash
./bazel-bin/lhd/lhd compile verilog \
  "$OUT/cva6_tlb_gate.lowered.v" \
  --reader yosys-verilog \
  --top cva6_tlb_gate \
  --workdir   "$OUT/e2e/work" \
  --result-json "$OUT/e2e/result.json" \
  --emit-dir lg:"$OUT/e2e/lgdb" \
  --emit-dir isabelle:"$OUT/e2e/isabelle" \
  --set isabelle.strict=true \
  --set isabelle.normalize=true \
  --set isabelle.cert_wf=skip
```

**Verified results (SV39 TLB, this machine):**

| Step | Outcome |
|---|---|
| `sv2v --top=cva6_tlb_gate -D VERILATOR …` | **exit 0**, `cva6_tlb_gate.lowered.v` = 750 lines / 2.5 MB, 2 monomorphized modules, 0 warnings. |
| `lhd … --reader yosys-verilog` | **stalls in `read_verilog -sv`** (CPU-bound, >2 min and climbing, ~1 GB RSS). |

**Why it stalls — the practical wall of this route:** sv2v flattens CVA6's
packed structs into a handful of *enormous* expressions — the lowered TLB has a
**single 489,823-character line**. Yosys's `read_verilog` parser + `proc`/`opt`
scale badly on lines that wide, so the front-end that is instant for Dino
(small firtool lines) chokes here. The slang front-end reached `pass.isabelle`
for the **same** TLB gate in *seconds* (`generated/cva6_module_isabelle_cva6_tlb_gate_sv39/`,
status `pass`, 1673-line `cva6_tlb_gate_Lgraph.thy`), because slang consumes the
structs natively and never materializes the half-megabyte line.

**Bottom line / route selection:**

| Route | Whole core (`cv64a6_imafdc_sv39_hpdcache_wb`) | Single module (e.g. TLB) |
|---|---|---|
| **slang + gate wrapper** | Elaborates, then dies at `pass/cprop/cprop.cpp:459` (EQ-fold bug) | **Works, fast** — proven to `pass.isabelle` (1673-line `.thy`) |
| **sv2v + `yosys-verilog`** (this doc) | sv2v dies on `acc_dispatcher` CV-X-IF structs | sv2v lowers cleanly, but `read_verilog` stalls on the wide flattened line |

So the sv2v/verilog route is the right answer to "no bender, no hand-written
struct wrappers," and the lowering itself is clean — but to make it *land* in
`pass.isabelle` you must tame the flattened-expression width (see §3.4). For
getting a module model **today**, the slang+gate route is faster and proven.

### 3.4 Taming sv2v's wide expressions (so `read_verilog` finishes)

The stall is width, not logic. Options, cheapest first:

- **Give `read_verilog` time.** It is CPU-bound and does make progress; for a
  single small module it can finish in minutes. Acceptable for a one-off model.
- **Split structs before flattening:** keep more module boundaries so no single
  module accumulates a giant flattened expression — i.e. lower a *narrower*
  `--top` (e.g. just `cva6_tlb`, not a gate that also pulls helper logic into
  one cone), or lower per-leaf and let yosys re-link via `hierarchy`.
- **`sv2v -E Always`** (and friends) reduce some rewrites; measure the line
  width (`awk '{if(length>m)m=length}END{print m}' file.v`) before/after.
- **Last resort:** post-process the lowered `.v` to break the widest assigns,
  or fall back to the slang+gate route for that module.

Note the differences from the slang runner: **no `--set yosys.filelist_file`,
no `--ignore-assertions`, no `-- <slang flags>`** — the source is one
self-contained lowered `.v`, and the reader is `yosys-verilog`.

---

## 4. When you must isolate a module sv2v can't reach from a bound top

A bare leaf (`--top=cva6_mmu`) leaves `CVA6Cfg`/`type` params at their `logic`
defaults → degenerate 1-bit ports. Two options, in order of preference:

1. **One-line instantiation top** (what `cva6_tlb_gate.sv` is): instantiate the
   module with concrete params from `build_config_pkg::build_config(...)` and
   let sv2v monomorphize. This is *not* the old wrapper style — you write only
   the instantiation, never the struct typedefs/tie-offs.
2. **Lower a larger bound subtree, then select downstream:** lower a parent that
   binds the params, then in the verilog front-end use
   `hierarchy -top <submodule>; flatten` to pick the submodule out. (Useful when
   the submodule is hard to instantiate standalone.)

To extend MMU coverage past the TLB, add `cva6_ptw_gate.sv`,
`cva6_shared_tlb_gate.sv`, and a `cva6_mmu_gate.sv` (binds the icache/dcache
`type` params from the config), each as a one-line instantiation top, and run
§3.2 + §3.3 per gate. All three are accelerator-free, so `--top` pruning keeps
sv2v happy.

---

## 5. Quick checklist

- [ ] Use the SV39 static flist (`cv64a6_imafdc_sv39_hpdcache_wb.top_cva6.flistplus.f`); never SV32.
- [ ] Translate `+incdir+`/`+define+`/paths → `-I`/`-D`/positional for sv2v.
- [ ] Always pass `-D VERILATOR` (skips the tracer class).
- [ ] Use `--top=<module>` to prune the CV-X-IF accelerator out of the cone.
- [ ] Feed the lowered `.v` to `lhd --reader yosys-verilog` (not `yosys-slang`).
- [ ] Whole-core (`--top=cva6`) is blocked: sv2v on `acc_dispatcher`, slang on `cprop.cpp:459`.

## 6. Paths

| Thing | Path |
|---|---|
| sv2v | `/mada/software/verilog/sv2v` |
| static flists | `livehd-new/generated/cva6_filelists/*.flistplus.f` |
| module runner | `livehd-new/scripts/run_cva6_module_isabelle_stress.sh` |
| full-core runner | `livehd-new/scripts/run_cva6_complex_isabelle_stress.sh` |
| gate tops | `livehd-new/scripts/cva6_module_wrappers/` |
| sv2v scratch / lowered V | `livehd-new/generated/cva6_sv2v_lower_sv39/` |
| Dino reference invocation | `generated/dino_lgraph_isabelle_livehd_new_20260605/logs/*.direct.log` |
