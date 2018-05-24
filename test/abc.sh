#!/bin/bash
declare -a inputs=("trivial.v" "test.v"\
                   "shift.v" "wires.v" "assigns.v" "consts.v" \
                   "simple_add.v"  \
                   "reduce.v" \
                   #"submodule.v" "multiport.v" "null_port.v" "trivial2.v" \
                   "gcd.v" "common_sub.v" "gcd_small.v" \
                   #"gcd_large.v" \
                   "gates.v" "operators.v" \
                   #"offset.v" "submodule_offset.v" "mem.v" "mem2.v" \
                   )
input_root=./inou/yosys/tests
YOSYS=./inou/yosys/lgyosys
ABC=./inou/abc/lgaig
CHECK=./inou/abc/abc_check
json=./inou/json/lgjson


for input in ${inputs[@]}
do

  base=${input%.*}
  rm -rf ./lgdb/ ./logs ./yosys-test ./*.json ./*.v ./*.blif

  if ! ${YOSYS} --techmap ${input_root}/${input}
  then
    echo "failed"
    exit 1
  fi

  if ! ${ABC} --lgdb ./lgdb --graph_name ${base}
  then
    echo "failed"
    exit 1
  fi

  if ! ${json} --lgdb ./lgdb --graph_name ${base}_mapped
  then
    echo "failed"
    exit 1
  fi

  if [[ $(${CHECK} golden.blif mapped.blif | grep -c 'Successfully matched generated verilog with yosys elaborated verilog file') -eq 1 ]]
  then
     rm -rf ./temp.blif
     echo "Successfully matched generated verilog with yosys elaborated verilog file"
  else
     rm -rf ./temp.blif
     echo "FAIL: Equivalence check failed"
     exit 1
  fi
done
rm -rf ./*.genlib
exit 0
