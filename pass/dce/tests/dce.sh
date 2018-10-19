#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

declare -a inputs=("common_sub.v")

LGCHECK=./inou/yosys/lgcheck
YOSYS=./inou/yosys/lgyosys
LGSHELL=./bazel-bin/main/lgshell

TEST_DIR=./pass/dce/tests

if [ ! -f ${LGSHELL} ]; then
  if [ -f ./main/lgshell ]; then
    LGSHELL=./main/lgshell
  else
    echo "could not find lgshell on $(pwd)"
    exit 1
  fi
fi

mkdir -p dce
for input in ${inputs[@]}
do
  base=${input%.*}
  ${YOSYS} ${TEST_DIR}/${input}

  if [ $? -eq 0 ]; then
    echo "Successfully created graph from ${input}"
  else
    echo "FAIL: lgyosys terminated with and error"
    exit 1
  fi

  echo "lgraph.open name:common_sub |> inou.json.fromlg output:dce_eg.json" |${LGSHELL}

  if [ $? -eq 0 ]; then
    echo "Successfully generated json file ${input}"
  else
    echo "WARN: json generation terminated with and error"
  fi

  echo "lgraph.open name:common_sub |> pass.dce |> inou.yosys.fromlg odir: dce" |${LGSHELL}
  if [ $? -eq 0 ]; then
    echo "Successfully ran dce on $input"
  else
    echo "FAIL: dce terminated with and error ${input}"
    exit 1
  fi

  ${LGCHECK} --implementation=dce/${base}.v --reference=${TEST_DIR}/${base}.v
  if [ $? -eq 0 ]; then
    echo "Successfully matched generated verilog with original verilog (${input})"
  else
    echo "FAIL: circuits are not equivalent (${input})"
    exit 1
  fi

done

echo "SUCCESS: all dce test cases ended without errors"

