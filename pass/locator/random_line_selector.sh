#!/bin/bash


random_line_selector_for_RocketTile() {
  echo ""
echo "*********************************"
echo "Running random_line_selector_for_RocketTile (hardcoded for RocketTile)"
echo "************"
echo ""
mkdir eval_files_tmp_RT
cd ../rocket-chip/src/main/scala/
#get chisel assignments from all .scala files 
grep -rHEn " := |===|=/=" * | grep ".scala" > all_grepped.log
mv all_grepped.log ../../../../livehd/eval_files_tmp_RT/.
#get the list of scala files used from original verilog
cd ../../../vsim/generated-src-asicNfpga-mada0/
grep ".scala" freechips.rocketchip.system.DefaultConfig.v > ../../../livehd/eval_files_tmp_RT/src_file_list_from_orig_ver.txt
cd ../../../livehd/eval_files_tmp_RT/
#manually did:    :v:\/\/ @\[:d
#                 :g:Monitor.scala:d
#                 :%s:.*src/main/scala/\(.[^ ]*.scala\).*:\1:g
sort -o src_file_list_from_orig_ver.txt src_file_list_from_orig_ver.txt
# modules used in pipelined cpu:
uniq src_file_list_from_orig_ver.txt > src_file_list_from_orig_ver_uniq.txt
rm src_file_list_from_orig_ver.txt

# modules used in RT:(with the help of src_file_list_from_orig_ver_uniq.txt)
# :%s:\n:|:g
# added import dontTouch to all these files (one time process so did manually)
# grep -E "..." all_grepped.log > all_grepped_for_RT.log
#:now we have all relevant chisel assignments and comparisons
rm all_grepped.log
#manually, in all_grepped_for_RT:
#:g: \:= true.B:d
#:g: \:= false:B:d
#
}


random_line_selector_for_singlecycleCPU() {
  echo ""
echo "*********************************"
echo "Running random_line_selector_for_singlecycleCPU (hardcoded for singlecycle CPU)"
echo "************"
echo ""
mkdir eval_files_tmp_SCCPU
cd ../dinocpu/src/main/scala/
#get chisel assignments from all .scala files 
grep -rHEn " := |===|=/=" * | grep ".scala" > all_grepped.log
mv all_grepped.log ../../../../livehd/eval_files_tmp_SCCPU/.
#get the list of scala files used from original verilog
cd ../../../../bazel_rules_hdl_test/dino/
grep ".scala" top.v > ../../livehd/eval_files_tmp_SCCPU/src_file_list_from_orig_ver.txt
cd ../../livehd/eval_files_tmp_SCCPU/
sed -i 's/^.*@\[//g' src_file_list_from_orig_ver.txt
sed -i 's/\.scala.*//g' src_file_list_from_orig_ver.txt
sort -o src_file_list_from_orig_ver.txt src_file_list_from_orig_ver.txt
# modules used in pipelined cpu:
uniq src_file_list_from_orig_ver.txt > src_file_list_from_orig_ver_uniq.txt
rm src_file_list_from_orig_ver.txt

# modules used in singlecycle cpu:(with the help of src_file_list_from_orig_ver_uniq.txt)
# added import dontTouch to all these files (one time process so did manually)
#    components/alu.scala
#    components/alucontrol.scala
#    memory/base-memory-components.scala
#    components/control.scala
#    single-cycle/cpu.scala
#    components/helpers.scala
#    memory/memory.scala
#    memory/memory-combin-ports.scala
#    components/nextpc.scala
#    components/register-file.scala
grep -E "single-cycle/cpu.scala|components/control.scala|components/register-file.scala|components/alu.scala|components/alucontrol.scala|components/helpers.scala|components/nextpc.scala|memory/base-memory-components.scala|memory/memory-combin-ports.scala|memory/memory.scala" all_grepped.log > all_grepped_for_singlecycleCPU.log
#:now we have all relevant chisel assignments and comparisons
rm all_grepped.log
}



random_line_selector_for_pipelinedCPU() {
echo ""
echo "*********************************"
echo "Running random_line_selector_for_pipelinedCPU (hardcoded for pipelined CPU)"
echo "************"
echo ""
mkdir eval_files_tmp
cd ../dinocpu/src/main/scala/
#get chisel assignments from all .scala files 
grep -rHEn " := |===|=/=" * | grep ".scala" > all_grepped.log
mv all_grepped.log ../../../../livehd/eval_files_tmp/.
#get the list of scala files used from original verilog
cd ../../../generated_pipelined_default/
grep ".scala" Top.v > ../../livehd/eval_files_tmp/src_file_list_from_orig_ver.txt
cd ../../livehd/eval_files_tmp/
sed -i 's/^.*@\[//g' src_file_list_from_orig_ver.txt
sed -i 's/\.scala.*//g' src_file_list_from_orig_ver.txt
sort -o src_file_list_from_orig_ver.txt src_file_list_from_orig_ver.txt
# modules used in pipelined cpu:
uniq src_file_list_from_orig_ver.txt > src_file_list_from_orig_ver_uniq.txt
rm src_file_list_from_orig_ver.txt

# modules used in pipelined cpu:(with the help of src_file_list_from_orig_ver_uniq.txt)
# added import dontTouch to all these files (one time process so did manually)
#     pipelined/cpu.scala
#     pipelined/stage-register.scala
#     components/control.scala
#     components/register-file.scala
#     components/alu.scala
#     components/alucontrol.scala
#     components/helpers.scala
#     components/nextpc.scala
#     components/forwarding.scala
#     components/hazard.scala
#     memory/base-memory-components.scala
#     memory/memory-combin-ports.scala
#     memory/memory.scala
grep -E "pipelined/cpu.scala|pipelined/stage-register.scala|components/control.scala|components/register-file.scala|components/alu.scala|components/alucontrol.scala|components/helpers.scala|components/nextpc.scala|components/forwarding.scala|components/hazard.scala|memory/base-memory-components.scala|memory/memory-combin-ports.scala|memory/memory.scala" all_grepped.log > all_grepped_for_pipelinedCPU.log
#:now we have all relevant chisel assignments and comparisons
rm all_grepped.log

#file: default_matching_map.log : the matching map from default_match in PipelinedCPU_23mar_critOnlyOptimization.log -- update: PipelinedCPU_18may.log

cd ../
python3 ./pass/locator/random_line_selector.py > eval_files_tmp/random_line_selector.log
rm eval_files_tmp/all_grepped_for_pipelinedCPU.log

echo ""
echo "CURRENT STATUS:"
echo "     all chisel assignments involved in pipelinedCPU from dinocpu/ have been evaluated."
echo "     The assignments NOT present in synth N/L are captured and randomly selected 1 per file."
echo "NEXT STEP:"
echo "     see eval_files_tmp/random_line_selector.log and "
echo "     select which files do you want to set DT to."
echo "     manually set DT to those lines (IMPORTANT: delete previously set DT from all files, if any!)"
echo "     THEN create V_orig by running following steps:"
echo "           cd ../dinocpu/"
echo "           singularity run library://jlowepower/default/dinocpu"
echo "           sbt:dinocpu> runMain dinocpu.elaborate pipelined"
echo "     Store the Top.* files created to a new folder in generated_pipelined_DT/"
echo "     Insert KEEP in Top.v"
echo "     THEN run the next function in this(pass/locator/random_line_selector.sh) file."
echo "*********************************"
echo ""
}

next_steps() {
  echo ""
  echo "*********************************"
  echo "Create V_Synth"
  echo "*********"
  echo ""

  Vorig=accToRandLineSel1/Top.v
  VorigName=top_1
  cd ../bazel_rules_hdl_test/dino_pipeline
  echo "WARNING: CHECK: "
  echo "               processing original verilog: ../dinocpu/generated_pipelined_DT/${Vorig}"
  echo "               copying Vorig as: ${VorigName}.v"
  read -p "Do you want to continue with these files?[y/n]" -n 1 -r
  echo ""
  if [[ ! $REPLY =~ ^[Yy]$ ]]
  then
    [[ "$0" = "$BASH_SOURCE" ]] && exit 1 || return 1 # handle exits from shell or function but don't exit interactive shell
  fi
  cp ../../dinocpu/generated_pipelined_DT/${Vorig} ${VorigName}.v
  sed -i 's/\<top_DT.v\>/${VorigName}/g' BUILD
  cd ../
  bazel build //dino_pipeline:verilog_PipelinedCPU_synth
  mkdir generatedWithKeep/eval_gen_${VorigName}
  cp -f bazel-out/k8-fastbuild/bin/dino_pipeline/verilog_PipelinedCPU_synth_synth_output.v generatedWithKeep/eval_gen_${VorigName}/PipelinedCPU.v

  #just to revert back for script reuse:
  cd dino_pipeline/
  sed -i 's/${VorigName}/top_DT.v/g' BUILD


  

  echo ""
  echo "CURRENT STATUS:"
  echo "       Generated Vsynth in bazel_rules_hdl_test/generatedWithKeep/eval_gen_${VorigName}"
  echo "NEXT STEP:"
  echo "       Find required node and set attr in color.json"
  echo "       THEN use next method in this file to generate orig .pb file"
  echo "*********************************"
  echo ""
}


random_line_selector_for_pipelinedCPU
#next_steps
