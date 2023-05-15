# To create DINO (https://github.com/jlpteaching/dinocpu-wq21.git) examples

1. Why we need small memory?
   We need to synthesize using Yosys. Large memory might be limiting factor for
   it. Also, we proceed with Dino1Core (not 120 cores).

## Steps to create DINO-top firrtl and verilog  

1. git clone https://github.com/jlpteaching/dinocpu-wq21
2. cd dinocpu-wq21
3. vim src/main/scala/configuration.scala
4. In this file, minSize is 1<<16 (i.e 64kB). Change 16 to 8. Save and close file.
`
-  def getNewMem(minSize: Int = 1 << 16): BaseDualPortedMemory = {
+  def getNewMem(minSize: Int = 1 << 8): BaseDualPortedMemory = { 
`
  4.1. Want to change cpuType and breanchPrediction type? look in
  src/main/scala/configuration.scala


For single-cycle CPU:
5. /dinocpu-wq21$ singularity run library://jlowepower/default/dinocpu
6. sbt:dinocpu> runMain dinocpu.elaborate single-cycle
7. Generated files will be available in the root folder as
   Top.anno.json                        Top.fir
   Top.DualPortedCombinMemory.memory.v  Top.v

## Steps to create Synth Netlist from above created original-verilog

1. git clone https://github.com/renau/bazel_rules_hdl_test 
2. cd bazel_rules_hdl_test ; mkdir dino
3. cp Top.v to bazel_rules_hdl_test/dino/ ;  mv Top.v top.v
4. vim dino/BUILD (contents for SingleCycleCPU example):
`
load("@rules_hdl_pip_deps//:requirements.bzl", "requirement")
load("@rules_hdl//synthesis:build_defs.bzl", "synthesize_rtl")
load("@rules_hdl//verilog:providers.bzl", "verilog_library")
synthesize_rtl(
    name = "verilog_SingleCycleCPU_synth",
    top_module = "SingleCycleCPU",
    deps = [
        ":verilog_SingleCycleCPU",
    ],
)
verilog_library(
    name = "verilog_SingleCycleCPU",
    srcs = [
        "top.v",
    ],
)
`
5. /bazel_rules_hdl_test$ bazel build //dino:verilog_SingleCycleCPU_synth
6. bazel_rules_hdl_test$ cp -f bazel-out/k8-fastbuild/bin/dino/verilog_SingleCycleCPU_synth_synth_output.v SingleCycleCPU.v
7. corresponding liberty file: /bazel_rules_hdl_test$ sky130_fd_sc_hd__ff_100C_1v95.lib

## Steps to create SingleCycleCPU.fir

Since we have Top.fir/Dino.fir and need to extract SingleCycleCPU.fir, following
script is required:

1. In livehd_regression/fir_regression/scripts/, copy the Top.fir from dinocpu-wq21 
2. ./extract_module.py -i Top.fir -t SingleCycleCPU
3. Leads to file formed: SingleCycleCPU.fir

## Steps to create SingleCycleCPU.ch.pb (from SingleCycleCPU.fir)

1. Go to livehd_regression/fir_regression/
2. put firrtl code (like SingleCycleCPU.fir generated above) in the chirrtl_src directory 

### Using chr_gen.sh script

1. (chr_gen means coming from chirrtl directory & chs_gen means coming from chisel_src directory. so if you have chisel then get pb and V directly from chs_gen.sh)
2. ./chr_gen.sh chirrtl_src/SingleCycleCPU.fir
3. (go over script before running it because of directory structure you might
   have made, and make changes accordingly) 

### Without script (example: SingleCycleCPU)

1.  cd ../tools/
2.  rm -rf firrtl* chisel*
3.  git clone https://github.com/chipsalliance/chisel3.git
4.  git clone https://github.com/chipsalliance/firrtl.git
5.  cp WritePB.scala firrtl/src/main/scala/firrtl/transforms/
    OR: firrtl$ cp ../livehd/inou/firrtl/WritePB.scala src/main/scala/firrtl/transforms/.
6.  make -C firrtl build
7.  cd firrtl    ; sbt compile ; sbt assembly    ; sbt publishLocal
8.  cd ../ 
9.  make -C chisel3 compile
10. cd chisel3 ; sbt compile ; sbt publishLocal       
11. cd ../../fir_regression
12. ../tools/firrtl/utils/bin/firrtl -i chirrtl_src/SingleCycleCPU.fir -X none --custom-transforms firrtl.transforms.WriteChPB
13. mv circuit.ch.pb generated/SingleCycleCPU.ch.pb
    mv SingleCycleCPU.fir  SingleCycleCPU.fir [do not use this fir file. some LoC are not present in this file]
 
