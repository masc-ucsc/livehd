#!/bin/bash

declare -a inputs=("common_sub.v")

for input in ${inputs[@]}
do
  ./inou/yosys/lgyosys ./pass/lgopt_gvn_pre/tests/${input}

  if [ $? -eq 0 ]; then
    echo "Successfully created graph from "${input}
  else
    echo "FAIL: lgyosys terminated with and error"
    exit 1
  fi

  ./inou/json/lgjson --lgdb lgdb --graph_name common_sub --json_output common_sub.json

  if [ $? -eq 0 ]; then
    echo "Successfully lgjson file "$a
  else
    echo "FAIL: lgjson terminated with and error"
    exit 1
  fi

  ./pass/lgopt_gvn_pre/lgopt_gvn_pre --lgdb lgdb --graph_name common_sub
  if [ $? -eq 0 ]; then
    echo "Successfully ran lgopt_gvn_pre file "$a
  else
    echo "FAIL: lgopt_gvn_pre terminated with and error"
    exit 1
  fi

  base=${input%.*}
  ./inou/yosys/lgyosys -g${base}

  if [ $? -eq 0 ]; then
    echo "Successfully created verilog from graph "${input}
  else
    echo "FAIL: lgyosys terminated with and error"
    exit 1
  fi

  yosys_read="read_verilog -sv ${base}.v; flatten; design -stash gold;
  read_verilog -sv ./pass/lgopt_gvn_pre/tests/${base}.v; flatten; design -stash gate;
  design -copy-from gold -as gold ${base}; design -copy-from gate -as gate ${base}"

  yosys_prep="flatten; proc; memory -nomap;
  equiv_make gold gate equiv;
  prep -flatten -top equiv;
  hierarchy -top equiv; hierarchy -check; flatten; proc; opt_clean;"

  yosys_equiv="equiv_simple;"
  yosys_equiv_extra="${yosys_simple}; equiv_simple -seq 5; equiv_induct -seq 5;"

  #try fast script first, if it fails, goes to more complex one
  ./subs/yosys/bin/yosys -p "${yosys_read}; ${yosys_prep}; ${yosys_equiv}; equiv_status -assert" \
    2> /dev/null | grep "Equivalence successfully proven!"
  if [ $? -eq 0 ]; then
    echo "Successfully matched generated verilog with original verilog ("${input}")"
  else
    echo "FAIL: circuits are not equivalent ("${input}")"
    exit 1
  fi

done

echo "SUCCESS: all gvn_pre test cases ended without errors"

