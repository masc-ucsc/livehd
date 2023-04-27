#!/bin/bash

#INPUTS:
FILENAME=SingleCycleCPU_flattened #SingleCycleCPU_flattened #PipelinedCPU_flattened #MaxPeriodFibonacciLFSR
MODULE_NAME=SingleCycleCPU #SingleCycleCPU #PipelinedCPU #MaxPeriodFibonacciLFSR
PERCENTAGE_CHANGE=0


SRCLOCATION=/soe/sgarg3/code_gen/new_dir/livehd/pass/locator/tests
DESTLOCATION=/soe/sgarg3/code_gen/new_dir/livehd/pass/locator/tests/dummy

if [ ! -f "${SRCLOCATION}/${FILENAME}.v" ];
then
  echo "Could not find ${SRCLOCATION}/${FILENAME}.v"
  exit 1
fi
sed 's/(\*.*\*)//g' ${SRCLOCATION}/${FILENAME}.v > ${DESTLOCATION}/${FILENAME}_1.v #remove (*...*)
sed -i 's/\/\*.*\*\///g' ${DESTLOCATION}/${FILENAME}_1.v #remove /*...*/
sed -i 's/\/\/.*//g' ${DESTLOCATION}/${FILENAME}_1.v #remove //...

python3 pass/locator/test_NL2NL.py ${DESTLOCATION}/${FILENAME}_1 ${PERCENTAGE_CHANGE}
#new file generated: @ DESTLOCATION : FILENAME_1_new.v
rm ${DESTLOCATION}/${FILENAME}_1.v
sed -i 's/module \(.*\)(/module \1_changedForEval(/g' ${DESTLOCATION}/${FILENAME}_1_new.v

if [ ! -f "${DESTLOCATION}/${FILENAME}_1_new.v" ];
then
  echo "Could not find ${DESTLOCATION}/${FILENAME}_1_new.v"
  exit 1
fi
ORIG_NL=${MODULE_NAME}
NEW_NL=${ORIG_NL}_changedForEval

# #bbmdbg:
# CXX=clang++ CC=clang bazel build -c dbg //main:lgshell
# bbm with opt:
CXX=clang++ CC=clang bazel build --define=profiling=1 -c opt //main:lgshell

echo "FIRST: ${SRCLOCATION}/${FILENAME}.v -- ${ORIG_NL}"
echo "SECOND: ${DESTLOCATION}/${FILENAME}_1_new.v -- ${NEW_NL}"
echo "LOG: ${DESTLOCATION}/${FILENAME}_${PERCENTAGE_CHANGE}.log"
echo ""
echo "STARTING @"
echo `date`
echo ""
echo ""

if [ ${FILENAME} == "MaxPeriodFibonacciLFSR" ]
then 
  echo ""
  echo "Running for MaxPeriodFibonacciLFSR:"
  echo ""
  rm -r lgdb/
  ./bazel-bin/main/lgshell "inou.liberty files:sky130.lib" 
  ./bazel-bin/main/lgshell " inou.yosys.tolg files:${SRCLOCATION}/${FILENAME}.v |> pass.bitwidth |> pass.cprop |> pass.bitwidth                           
  inou.yosys.tolg files:${DESTLOCATION}/${FILENAME}_1_new.v |> pass.bitwidth |> pass.cprop |> pass.bitwidth                                                           
  lgraph.open name:${ORIG_NL} |> lgraph.open name:${NEW_NL} |> inou.graphviz.from odir:tmp_1 |> inou.traverse_lg LGorig:${ORIG_NL} LGsynth:${NEW_NL} 
  " > ${DESTLOCATION}/${FILENAME}_${PERCENTAGE_CHANGE}.log   
elif [ ${FILENAME} == "PipelinedCPU_flattened" ]
then
  echo ""
  echo "Running for pipelined_flattened:"
  echo ""
  rm -r lgdb/ 
  ./bazel-bin/main/lgshell "inou.liberty files:sky130_fd_sc_hd__ff_100C_1v95.lib"
  perf record -g ./bazel-bin/main/lgshell " inou.yosys.tolg files:${SRCLOCATION}/${FILENAME}.v |> pass.cprop |> pass.bitwidth |> pass.cprop |> pass.bitwidth                           
  inou.yosys.tolg files:${DESTLOCATION}/${FILENAME}_1_new.v |> pass.cprop |> pass.bitwidth |> pass.cprop |> pass.bitwidth                                                           
  lgraph.open name:${ORIG_NL} |> lgraph.open name:${NEW_NL} |> inou.graphviz.from odir:tmp_1 |> inou.traverse_lg LGorig:${ORIG_NL} LGsynth:${NEW_NL} 
  " > ${DESTLOCATION}/${FILENAME}_${PERCENTAGE_CHANGE}.log 
elif [ ${FILENAME} == "SingleCycleCPU_flattened" ]
then
  echo ""
  echo "Running for SingleCycleCPU_flattened:"
  echo ""
  rm -r lgdb/ 
  ./bazel-bin/main/lgshell "inou.liberty files:sky130_fd_sc_hd__ff_100C_1v95.lib"
  perf record -g ./bazel-bin/main/lgshell " inou.yosys.tolg files:${SRCLOCATION}/${FILENAME}.v |> pass.cprop |> pass.bitwidth |> pass.cprop |> pass.bitwidth                           
  inou.yosys.tolg files:${DESTLOCATION}/${FILENAME}_1_new.v |> pass.cprop |> pass.bitwidth |> pass.cprop |> pass.bitwidth                                                           
  lgraph.open name:${ORIG_NL} |> lgraph.open name:${NEW_NL} |> inou.graphviz.from odir:tmp_1 |> inou.traverse_lg LGorig:${ORIG_NL} LGsynth:${NEW_NL} 
  " > ${DESTLOCATION}/${FILENAME}_${PERCENTAGE_CHANGE}.log
fi
echo ""
echo "ENDING @"
echo `date`
echo ""
echo ""