
## To read CVA6

Use the yosys-slang flow:
```
export CVA6_REPO_DIR=~/tmp/tmp_cva6/repo/
export HPDCACHE_DIR=$CVA6_REPO_DIR/core/cache_subsystem/hpdcache/

./bazel-bin/lhd/lhd compile --reader yosys-slang --diag-fmt pretty --top cva6 --emit-dir lg:cva_lg1 -- --ignore-unknown-modules --allow-use-before-declare $CVA6_REPO_DIR/core/include/cv64a6_imafdc_sv39_wb_config_pkg.sv -F $CVA6_REPO_DIR/core/Flist.cva6 -DSYNTHESIZE
```

## To read Dino Dual issue

```
./bazel-bin/lhd/lhd compile --top PipelinedDualIssueCPU --emit-dir lg:dino_v1 -- -F ../simplechisel/build_dualissue_d/filelist.f -DSYNTHESIS
./bazel-bin/lhd/lhd compile --top PipelinedDualIssueCPU --emit-dir pyrope:tmpx -- -F ../simplechisel/build_dualissue_nd/filelist.f -DSYNTHESIS -Wno-implicit-conv -Wno-unconnected-port
```

Potential Pyrope implementation:

```
./bazel-bin/lhd/lhd compile --top PipelinedDualIssueCPU --emit-dir lg:dino_p1 ../simplechisel/build_dualissue_d/prp2/cpu.prp
```

## To read XiangShang

Newer chisel/firrtl generation: (like using until reserved SV keyword)
```
 ./bazel-bin/lhd/lhd compile --top XSCore --emit-dir pyrope:xs_core_prp -- -F ../xs/repo/build/rtl/filelist.f -DSYNTHESIZE -Wno-implicit-conv -Wno-unconnected-port
```

## One-shot synthesis (`lhd synth`)

The fused flow — compile -> `pass color synth` -> `pass abc` -> `pass
opentimer` over one in-memory design. `--top` takes the bare entity, one
Liberty feeds both abc and opentimer (`synth.liberty`, default
`$HAGENT_TECH_DIR/sky130_fd_sc_hd__tt_025C_1v80.lib`), and with a `--workdir`
the compiled design, the mapped netlist and both reports land under
`W/synth/` while a re-run reuses everything unchanged (compile cache +
`abc_cache/`; `--set lhd.incremental=false` forces an honest cold run with the
same netlist):

```
./bazel-bin/lhd/lhd synth dino_prp/PipelinedDualIssueCPU.prp --top PipelinedDualIssueCPU --workdir W --stats
#   W/synth/lg        the compiled design   (lec reference: --ref lg:W/synth/lg)
#   W/synth/net       the mapped netlist    (--emit-dir lg:DIR relocates it)
#   W/synth/qor.json  pass.abc QoR;  W/synth/timing.json  the OpenTimer critical path
./bazel-bin/lhd/lhd synth lg:dino_lg --top PipelinedDualIssueCPU --emit-dir lg:dino_net --emit-dir report:rep
./bazel-bin/lhd/lhd synth dino_prp/PipelinedDualIssueCPU.prp --top PipelinedDualIssueCPU \
    --set abc.adder=cla --set synth.opentimer=false --emit verilog:net.v --result-json r.json
```

`--result-json`'s `qor` member is `{kind:"synth", abc:{...}, sta:{...}}` — each
sub-object exactly what the standalone pass embeds. The manual steps below stay
the way to run a different coloring (`synth` colors with `synth`, always) or
to look at an intermediate.

## Synthesis + timing QoR on one XiangShan module (2opt-freq loop)

The frequency-optimization loop primitives on `xs_core_prp/Alu.prp` (imports
resolve from sibling files in the same directory). `pass abc` uses
`$HAGENT_TECH_DIR/sky130_fd_sc_hd__tt_025C_1v80.lib` by default; add
`--set abc.library=FILE` for another Liberty.

```
# compile (the whole Alu hierarchy: AluDataModule, SubModule, AddModule, ...)
./bazel-bin/lhd/lhd compile xs_core_prp/Alu.prp --top Alu.Alu --recipe O1 --emit-dir lg:alu_lg --workdir alu_w

# tech-map: one region module per (module, color); QoR lands in alu_w/qor.json
# AND the --result-json envelope's "qor" member (per-region gates/area/critical
# delay + the critical output's source file:line)
./bazel-bin/lhd/lhd pass abc --top Alu.Alu lg:alu_lg --emit-dir lg:alu_net --workdir alu_w --result-json r.json

# accurate scorer: OpenTimer STA on ONE mapped module (pick a hot region from
# qor.json, e.g. SubModule) -> alu_w/timing.json {max_delay, critical_pin,
# critical_src "file:line", endpoints[]}
./bazel-bin/lhd/lhd pass opentimer --top 'SubModule.SubModule__c0' lg:alu_net $HAGENT_TECH_DIR/sky130_fd_sc_hd__tt_025C_1v80.lib --workdir alu_w
```

The loop gate (only LEC-passing edits survive). `lhd lec` reads `.prp` sides
directly — imports resolve from sibling files — but the loop pre-compiles to
lg: anyway so the abc/QoR steps reuse the same compiled tree:

```
./bazel-bin/lhd/lhd compile xs_core_prp/Alu.prp --top Alu.Alu --recipe O1 --emit-dir lg:alu_lg_edited --workdir alu_w2
./bazel-bin/lhd/lhd lec --impl lg:alu_lg_edited --ref lg:alu_lg --top Alu.Alu --workdir alu_w
# exit 0 + PROVEN -> accept the edit; equiv_fail -> reject (alu_w/lecfail.json has the witness)
```

Steering ABC without touching the source — per-region overrides keyed by color
(uncolored modules are color 0):

```
./bazel-bin/lhd/lhd pass abc --top Alu.Alu lg:alu_lg --emit-dir lg:alu_net2 --workdir alu_w3 \
    --set abc.region_opts='{"0":{"flow":"strash; &get -n; &fraig -x; &put; &get -n; &dc4; &dch -f; &nf {D}; &put","delay":"10"}}'
```

Or from the Pyrope source, a block attribute makes a code snippet its own
synthesis region with its own flow (see `../docs/docs/pyrope/04b-attributes.md`
for a worked sample, and `../docs/docs/livehd/08-synth.md` for the whole loop):

```
{::[abc='strash; &get -n; &dch -f; &nf {D}; &put', color=2]
  y = t ^ c
}
```

## Timing on the Dino dual-issue CPU (single region + whole-design)

Compile the Pyrope CPU, tech-map every module, then run OpenTimer. `pass abc`
keeps the design hierarchical (each module `M` becomes a wrapper over its mapped
region `M__c0`); flops/memories stay native and are timed as zero-arrival path
boundaries.

```
# 1. compile Pyrope -> lg:
./bazel-bin/lhd/lhd compile dino_prp/PipelinedDualIssueCPU.prp --emit-dir lg:dino_lg

# 2. combinational ABC tech-map (-> region submodules)
./bazel-bin/lhd/lhd pass abc --top PipelinedDualIssueCPU.PipelinedDualIssueCPU \
    lg:dino_lg --emit-dir lg:dino_lg_abc --workdir dino_wd
```

Time ONE mapped module. Pick a region (`<mod>__c<N>`) from `qor.json`; the
report lands in `dino_wd/timing.json` (max_delay, critical_pin, `critical_src`
file:line, endpoints[]):

```
./bazel-bin/lhd/lhd pass opentimer --top ALU.ALU__c0 \
    lg:dino_lg_abc $HAGENT_TECH_DIR/sky130_fd_sc_hd__tt_025C_1v80.lib --workdir dino_wd
#   -> {"module":"ALU.ALU__c0","max_delay":25.0,"critical_pin":"g2975_...o32ai_1:Y",
#       "critical_src":"dino_prp/ALU.prp:2", ...}
```

Whole-design timing across the instance hierarchy — the default
(`pass.opentimer.hier=true`; `--set pass.opentimer.hier=false` instead rejects
a top with non-Liberty Subs and times one module per run). It flattens the
tree — combinational paths chain across module boundaries, so the critical
path can span several modules (here a datapath cone ending at a pipeline
register), and points back at the pre-synthesis RTL line. Flops and memories
stay zero-arrival path boundaries (memory read ports are virtual primary
inputs):

```
./bazel-bin/lhd/lhd pass opentimer --top PipelinedDualIssueCPU \
    lg:dino_lg_abc $HAGENT_TECH_DIR/sky130_fd_sc_hd__tt_025C_1v80.lib --workdir dino_wd
#   -> {"module":"PipelinedDualIssueCPU.PipelinedDualIssueCPU","max_delay":45.5,
#       "critical_pin":"...pipeA_ex_mem.u_StageReg_6.StageReg_6__c1.g...:X",
#       "critical_src":"dino_prp/StageReg_6.prp:13", ...}  # a cross-module cone
```

## Formal verify on one XiangShan module (assert/assume collateral + workdir results)

Pick a module from the compiled corpus (here `xs_core_prp/DelayN_1.prp`, a
1-cycle 336-bit delay register: `REG <= io_in`) and write the properties as a
sidecar of `formal` blocks — the collateral never touches the design file:

```
mkdir -p verif_delayn && cat > verif_delayn/DelayN_1.verify.prp <<'EOF'
const top = import("DelayN_1.DelayN_1")

formal delayn.bound {
  mut acc = top
  assume_nocheck(acc.io_in <= 15)                            // free env constraint by fiat (disclosed)
  assert(acc.REG <= 15, "delayed data respects the input bound")
}

formal delayn.probe {
  mut acc = top
  assert(acc.REG != 5, "delayed data never 5")               // reachable: refutes with a trace
}
EOF
```

Run (the sidecar is an extra positional; `--workdir` makes every artifact
persistent):

```
./bazel-bin/lhd/lhd formal verify xs_core_prp/DelayN_1.prp verif_delayn/DelayN_1.verify.prp \
    --top DelayN_1 --set formal.bound=6 --workdir delayn_w
#   assume at ...verify.prp:5 [delayn.bound]: in force (UNCHECKED assume_nocheck; ...)
#   assert at ...verify.prp:6 "'delayed data respects the input bound'" [delayn.bound]: PROVEN (inductive — every cycle of every bound)
#   assert at ...verify.prp:11 "'delayed data never 5'" [delayn.probe]: REFUTED at cycle 2
#     counterexample inputs: cyc0: ... io_in=0 | cyc1: ... io_in=5 | cyc2: ...
# exit 1 (a reachable violation fails the run; all-green exits 0)
```

Results in the workdir:

```
cat delayn_w/formal_report.json     # EVERY obligation: id kind@file:line[block], verdict
                                    # (proven/refuted/unknown/in_force), unbounded, cycles,
                                    # unknown_why, assume class, solve_ms, timeout core, artifacts
jq -r '.obligations[] | select(.verdict=="proven") | .id' delayn_w/formal_report.json   # the PASSED list
jq -r '.obligations[] | select(.verdict=="refuted") | [.id, .witness] | @tsv' delayn_w/formal_report.json
cat delayn_w/formalfail.json        # machine witness: root_cut {file,line,cycle} + per-cycle input trace
cat delayn_w/formalfail.prp         # self-contained `lhd sim` testbench driving that trace
open delayn_w/formalfail.vcd        # waveform of the replay (the runtime assert re-fires)
```

The testbench re-checks the violated formal-block obligation at the violating
cycle, so the replay FAILS — a refuted `assume` included, since an assume is
checked as an assert before it is used (`assume_nocheck` is the free spelling
and never reaches the testbench). Two obligations sharing ONE source line
cannot be told apart by `file:line`, so that case emits the driven trace with
no embedded check and says so in the file header; put one statement per line to
get the check back. A design-BODY assert is still not executed by `lhd sim` —
read those off the VCD.

Formal blocks are INDEPENDENT tests: each block's constraints apply only to
that block's own obligations, so two blocks may carry mutually-exclusive
`assume_nocheck` constraints and both still prove. Assumes written in the
DESIGN itself are the other tier — they are in force for every block. A block
whose own constraint set is contradictory is named and fails the run (its
proofs would be vacuous).

Because a block IS a test, it enumerates and selects exactly the way a `lhd sim`
`test` block does:

```
lhd formal verify DelayN_1.prp DelayN_1.verify.prp --list-tests
#   {"file":"DelayN_1.verify.prp","tests":[{"name":"delayn.bound","params":[],…},
#                                          {"name":"delayn.probe","params":[],…}]}
#   --list-tests is a pure PARSE: it never loads the design and never calls a solver
lhd formal verify DelayN_1.prp DelayN_1.verify.prp delayn.bound --top DelayN_1
#   a lone non-path positional runs just that block (fnmatch, so 'delayn.*' = a family)
```

A selector matching no block FAILS and lists the real names — it never degrades
into quietly proving only the design's own obligations.

Useful knobs: `--formal 'delayn.bound'` is the flag spelling of that selector; `--set formal.strict=true`
fails on UNKNOWN; `--set formal.spec_mining_timeout=15` diagnoses a stuck run (timeout
core in the report) and MINES inductive invariants into `delayn_w/formal_mined.prp`
(pass it back as another sidecar). EVERY `assume` is a proof obligation —
CHECKED as an assert before it is used, so a false one REFUTES instead of
silently faking a PROVEN, and an input-only constraint (never provable over
free inputs) refutes with a hint to spell it `assume_nocheck` — the explicit
UNCHECKED environment constraint. With `--workdir` a proven assume-check is
cached and skipped on warm re-runs.

## Formal verify with SUBMODULE-bound blocks (xs_core_prp Alu, RISC-V properties)

A block may target a submodule anywhere in the hierarchy: it binds to EVERY
instance (reported `[block@instance]`) and its signal paths reach the
instance's input/output PORTS as well as its registers. The sidecar
`verif_alu/Alu.verify.prp` states two RISC-V facts about the XiangShan ALU:

```
const data = import("AluDataModule.AluDataModule")
const sub  = import("SubModule.SubModule")

// constrain the env: only 32-bit ops reach the datapath (func[6:4]==1 selects
// the W group; func[3:0]==0 is ADDW, ==2 is SUBW) => the result is sign-extended.
formal alu.word_sext {
  mut acc = data
  assume_nocheck(acc.io_func#[4..=6] == 1)
  assume_nocheck(acc.io_func#[0..=3] == 0 or acc.io_func#[0..=3] == 2)
  assert(acc.io_result#[31..=63] == 0 or acc.io_result#[31..=63] == 0x1ffffffff,
         "ADDW/SUBW result is sign-extended")
}

// the subtractor is src1 + ~src2 + 1 over 65 bits: carry-out == no-borrow.
formal alu.sub_borrow {
  mut acc = sub
  assert(u1(acc.io_sub#[64]) == u1(acc.io_src#[0..=63] >= acc.io_src#[64..=127]),
         "subtractor carry-out == unsigned no-borrow")
}
```

Run it directly on the .prp — a Pyrope side resolves its own `import()`s from
sibling files (an `lg:DIR` design is also accepted, e.g. a pre-compiled tree):

```
./bazel-bin/lhd/lhd formal verify xs_core_prp/Alu.prp verif_alu/Alu.verify.prp --top Alu.Alu \
    --set formal.bound=2 --workdir alu_verif_w
#   assume at ...verify.prp:13 [alu.word_sext@aluModule]: in force (UNCHECKED assume_nocheck; ...)
#   assert at ...verify.prp:15 "'ADDW/SUBW result is sign-extended'" [alu.word_sext@aluModule]: PROVEN (inductive — every cycle of every bound)
#   assert at ...verify.prp:24 "'subtractor carry-out == unsigned no-borrow'" [alu.sub_borrow@aluModule.subModule]: PROVEN (inductive — every cycle of every bound)
# exit 0; alu_verif_w/formal_report.json has the machine records
```

Notes: the `assume_nocheck` constraints bind the AluDataModule INSTANCE's
`io_func` input port — spelled nocheck because a free-input pin can never be
PROVEN (a plain `assume` is checked as an assert first and would refute);
`alu.sub_borrow` binds a port two instance levels
deep (`aluModule.subModule`). A false port claim refutes with a trace like any
other obligation.

## To Generate Perfetto

Open livehd.trace at https://ui.perfetto.dev

```
bazel build -c opt --define profiling=1 //lhd:lhd
```

```
./bazel-bin/lhd/lhd compile --top XSCore xs_core_prp/XSCore.prp --emit-dir lg:tmp/
```

## Fixme tests

```
bazel query 'attr("tags", "fixme", tests(//...))'
bazel test --test_tag_filters=fixme -c opt //inou/prp:all
```
