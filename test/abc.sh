#!/bin/bash
declare -a inputs=("trivial.v" "simple_flop.v" "test.v"\
                   #"shift.v" "wires.v" "assigns.v" "trivial2.v" "consts.v" \   ## debug later
                   "simple_add.v"  \
                   "reduce.v" \
                   #"submodule.v" "multiport.v" "null_port.v" \
                   "gcd.v" "gcd_small.v" "gcd_large.v" "common_sub.v" \
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
  rm -rf ./lgdb/ ./logs ./yosys-test *.json *.v *.blif

  ${YOSYS} --techmap ${input_root}/${input}
  ${ABC} --lgdb ./lgdb --graph_name ${base}
  ${json} --lgdb ./lgdb --graph_name ${base}_mapped
  ${CHECK} golden.blif abc_output/${base}_post.blif | grep "Successfully matched generated verilog with yosys elaborated verilog file"
  if [ $? -eq 0 ]; then
     rm -rf temp.blif
     echo "Successfully matched generated verilog with yosys elaborated verilog file"
  else
     rm -rf temp.blif
     echo "FAIL: Equivalence check failed"
     exit 1
  fi
done
rm -rf *.genlib
exit 0
