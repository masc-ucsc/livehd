#!/bin/bash

#INPUTS:
FILENAME=MaxPeriodFibonacciLFSR
MODULE_NAME=MaxPeriodFibonacciLFSR
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

ORIG_NL=${MODULE_NAME}
NEW_NL=${ORIG_NL}_changedForEval
CXX=clang++ CC=clang bazel build -c dbg //main:lgshell
./bazel-bin/main/lgshell " inou.yosys.tolg files:${SRCLOCATION}/${FILENAME}.v |> pass.bitwidth |> pass.cprop |> pass.bitwidth                           
inou.yosys.tolg files:${DESTLOCATION}/${FILENAME}_1_new.v |> pass.bitwidth |> pass.cprop |> pass.bitwidth                                                           
lgraph.open name:${ORIG_NL} |> lgraph.open name:${NEW_NL} |>  inou.traverse_lg LGorig:${ORIG_NL} LGsynth:${NEW_NL} 
" > ${DESTLOCATION}/${FILENAME}_${PERCENTAGE_CHANGE}.log                                                                                                                                                                                                                                                                                              