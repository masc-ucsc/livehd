#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

declare -a inputs=("trivial.v" "test.v"\
                   "shift.v" "wires.v" "assigns.v" "consts.v" \
                   "simple_add.v" "reduce.v" \
                   #"submodule.v" "multiport.v" "null_port.v" "trivial2.v" \
                   "gcd.v" "common_sub.v" "gcd_small.v" \
                   #"gcd_large.v" \
                   "gates.v" "operators.v" \
                   #"offset.v" "submodule_offset.v" "mem.v" "mem2.v" \
                   )
input_root=./inou/yosys/tests
YOSYS=./inou/yosys/lgyosys
LGSHELL=./bazel-bin/main/lgshell
CHECK=./pass/abc/abc_check
json=./inou/json/lgjson

if [ ! -f ${LGSHELL} ]; then
  if [ -f ./main/lgshell ]; then
    LGSHELL=./main/lgshell
  else
    echo "could not find lgshell on $(pwd)"
    exit 1
  fi
fi

pwd
for input in ${inputs[@]}
do

  base=${input%.*}

  echo "${YOSYS} --techmap --top=${base} ${input_root}/${input}"
  ${YOSYS} --techmap --top=${base} ${input_root}/${input}

  if [ ! $? -eq 0 ]; then
    echo "yosys2lg failed ${base}"
    exit 1
  fi

  echo "lgraph.find path:lgdb name:${base} |> pass.abc blif_file:${base}_map.blif" | ${LGSHELL}

  if [ ! $? -eq 0 ]; then
    echo "lg2abc failed ${base}"
    exit 1
  fi

  #if ! ${json} --lgdb ./lgdb --graph_name ${base}_mapped
  #then
  #  echo "I was not able to generate json for ${base}"
  #fi

  if [[ $(${CHECK} ${base}.blif ${base}_map.blif | grep -c 'Successfully matched generated verilog with yosys elaborated verilog file') -eq 1 ]]
  then
     rm -rf ./temp.blif
     echo "Successfully matched generated verilog with yosys elaborated verilog file"
  else
     rm -rf ./temp.blif
     echo "FAIL: Equivalence check failed ${base}"
     exit 1
  fi
done

rm -rf ./*.genlib
rm -rf ./lgdb/ ./logs ./yosys-test ./*.json ./*.v ./*.blif

