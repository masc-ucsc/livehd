#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

echo "yosys.sh running in "$(pwd)

LGSHELL=./bazel-bin/main/lgshell

if [ ! -x $LGSHELL ]; then
  if [ -x ./main/lgshell ]; then
    LGSHELL=./main/lgshell
    echo "lgshell is in $(pwd)"
  else
    echo "FAILED: pyrope_test.sh could not find lgshell binary in $(pwd)";
  fi
fi

YOSYS=./inou/yosys/lgyosys
LGCHECK=./inou/yosys/lgcheck

rm -rf ./logs

inputs=inou/yosys/tests/*.v
long=""
if [ "$1" == "long" ]; then
  long="true"
  shift
fi
if [ "$1" != "" ]; then
  inputs=""
  while [ "$1" != "" ]; do
    inputs+=" "$1
    shift
  done
  echo "verilog.sh inputs: ${inputs}"
fi

for full_input in ${inputs}
do
  input=$(basename ${full_input})
  echo ${YOSYS} ./inou/yosys/tests/${input}
  base=${input%.*}

  if [[ $long == "" && $input =~ "long_" ]]; then
    echo "Skipping long test for "$base
    continue
  fi

  rm -rf lgdb_yosys tmp_yosys
  mkdir -p tmp_yosys

  #${YOSYS} ./inou/yosys/tests/${input} > ./yosys-test/log_from_yosys_${input} 2> ./yosys-test/err_from_yosys_${input}
  echo "inou.yosys.tolg path:lgdb_yosys top:${base} files:"${full_input}  | ${LGSHELL} -q
  if [ $? -eq 0 ]; then
    echo "Successfully created graph from ${input}"
  else
    echo "FAIL: lgyosys parsing terminated with an error (testcase ${input})"
    exit 1
  fi

  #./inou/json/lgjson  --graph_name ${base} --json_output ${base}.json > ./yosys-test/log_json_${input} 2> ./yosys-test/err_json_${input}
  #if [ $? -ne 0 ]; then
    #echo "WARN: Not able to create JSON for testcase ${input}"
  #fi

  #${YOSYS} -g${base} -h > ./yosys-test/log_to_yosys_${input} 2> ./yosys-test/err_to_yosys_${input}

  echo "lgraph.match path:lgdb_yosys |> inou.yosys.fromlg odir:tmp_yosys" | ${LGSHELL} -q
  if [ $? -eq 0 ]; then
    echo "Successfully created verilog from graph ${input}"
  else
    echo ${YOSYS} -g${base} -h -d
    echo "FAIL: verilog generation terminated with an error (testcase ${input})"
    exit 1
  fi
  $(cat tmp_yosys/*.v >tmp_yosys/all_${base}.v)

  if [[ $base =~ "nocheck_" ]]; then
    echo "Skipping check for "$base
  else
    ${LGCHECK} --implementation=tmp_yosys/all_${base}.v --reference=${full_input} --top=${base}
    if [ $? -eq 0 ]; then
      echo "Successfully matched generated verilog with original verilog (${full_input})"
    else
      echo "FAIL: circuits are not equivalent (${full_input})"
      exit 1
    fi
  fi

done

echo "SUCCESS: all yosys test cases ended without errors"

