#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

declare -a inputs=("common_sub.v")

LGCHECK=./inou/yosys/lgcheck
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

for input in ${inputs[@]}
do
  base=${input%.*}

  rm -rf tmp_dce
  mkdir -p tmp_dce

  echo "inou.yosys.tolg path:lgdb_dce files:${TEST_DIR}/${base}.v top:${base}" | ${LGSHELL} -q
  if [ $? -eq 0 ]; then
    echo "Successfully created graph from ${input}"
  else
    echo "FAIL: lgyosys terminated with and error"
    exit 1
  fi

  echo "lgraph.open path:lgdb_dce name:common_sub |> pass.dce |> inou.yosys.fromlg odir:tmp_dce" | ${LGSHELL}
  if [ $? -eq 0 ]; then
    echo "Successfully ran dce on $input"
  else
    echo "FAIL: dce terminated with and error ${input}"
    exit 1
  fi

  ${LGCHECK} --implementation=tmp_dce/${base}.v --reference=${TEST_DIR}/${base}.v --top=${base}
  if [ $? -eq 0 ]; then
    echo "Successfully matched generated verilog with original verilog (${input})"
  else
    echo "FAIL: circuits are not equivalent (${input})"
    exit 1
  fi

done

echo "SUCCESS: all dce test cases ended without errors"

