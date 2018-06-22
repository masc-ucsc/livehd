#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

declare -a inputs=("common_sub.v")

LGCHECK=./inou/yosys/lgcheck

for input in ${inputs[@]}
do
  ./inou/yosys/lgyosys ./pass/lgopt_dce/tests/${input}

  if [ $? -eq 0 ]; then
    echo "Successfully created graph from ${input}"
  else
    echo "FAIL: lgyosys terminated with and error"
    exit 1
  fi

  ./inou/json/lgjson --lgdb lgdb --graph_name common_sub --json_output dce_eg.json

  if [ $? -eq 0 ]; then
    echo "Successfully lgyaml file "$a
  else
    echo "FAIL: lgyaml terminated with and error"
    exit 1
  fi

  ./pass/lgopt_dce/lgopt_dce --lgdb lgdb --graph_name common_sub
  if [ $? -eq 0 ]; then
    echo "Successfully ran lgopt_dce file "$a
  else
    echo "FAIL: lgopt_dce terminated with and error"
    exit 1
  fi

   base=${input%.*}
  ./inou/yosys/lgyosys -g${base}

  if [ $? -eq 0 ]; then
    echo "Successfully created verilog from graph ${input}"
  else
    echo "FAIL: lgyosys terminated with and error"
    exit 1
  fi

  ${LGCHECK} --implementation=${base}.v --reference=./pass/lgopt_dce/tests/${base}.v
  if [ $? -eq 0 ]; then
    echo "Successfully matched generated verilog with original verilog (${input})"
  else
    echo "FAIL: circuits are not equivalent (${input})"
    exit 1
  fi



done

echo "SUCCESS: all dce test cases ended without errors"

