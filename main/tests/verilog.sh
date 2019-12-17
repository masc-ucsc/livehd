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
fixme=""
if [ "$1" != "" ]; then
  long="true"
  fixme="true"
  inputs=""
  while [ "$1" != "" ]; do
    inputs+=" "$1
    shift
  done
  echo "verilog.sh inputs: ${inputs}"
fi

pass=0
fail=0
fail_list=""
for full_input in ${inputs}
do
  input=$(basename ${full_input})
  echo ${YOSYS} ./inou/yosys/tests/${input}
  base=${input%.*}

  if [[ $input =~ "long_" ]]; then
    if [[ $long == "" ]]; then
      echo "Skipping long test for "$base
      continue
    fi
    base=${base:5}
  else
    if [[ $long == "true" ]]; then
      echo "Skipping short test for "$base
      continue
    fi
  fi
  if [[ $input =~ "fixme_" ]]; then
    if [[ $fixme == "" ]]; then
      echo "Skipping fixme test for "$base
      echo "PLEASE: Somebody fix this!!!"
      continue
    fi
    base=${base:6}
  fi
  if [[ $input =~ "nocheck_" ]]; then
    base=${base:8}
  fi

  rm -rf lgdb_yosys tmp_yosys
  mkdir -p tmp_yosys

  echo "inou.yosys.tolg path:lgdb_yosys top:${base} files:"${full_input}  | ${LGSHELL} -q >tmp_yosys/${input}.log 2>tmp_yosys/${input}.err
  if [ $? -eq 0 ]; then
    echo "Successfully created graph from ${input}"
  else
    echo "FAIL: lgyosys parsing terminated with an error (testcase ${input})"
    let fail++
    fail_list+=" "$base
  fi
  LC=$(grep -iv Warning tmp_yosys/${input}.err | grep -v "recommended to use " | wc -l | cut -d" " -f1)
  if [[ $LC -gt 0 ]]; then
    echo "FAIL: Faulty "$LC" err verilog file tmp_yosys/${input}.err"
    let fail++
    fail_list+=" "$base
    continue
  fi
  LC=$(grep -i signal tmp_yosys/${input}.log | wc -l | cut -d" " -f1)
  if [[ $LC -gt 0 ]]; then
    echo "FAIL: Faulty "$LC" log verilog file tmp_yosys/${input}.log"
    let fail++
    fail_list+=" "$base
    continue
  fi

  #./inou/json/lgjson  --graph_name ${base} --json_output ${base}.json > ./yosys-test/log_json_${input} 2> ./yosys-test/err_json_${input}
  #if [ $? -ne 0 ]; then
    #echo "WARN: Not able to create JSON for testcase ${input}"
  #fi

  #${YOSYS} -g${base} -h > ./yosys-test/log_to_yosys_${input} 2> ./yosys-test/err_to_yosys_${input}

  echo "lgraph.match path:lgdb_yosys |> inou.yosys.fromlg odir:tmp_yosys" | ${LGSHELL} -q 2>tmp_yosys/${input}.err
  LC=$(grep -iv Warning tmp_yosys/${input}.err | grep -v "recommended to use " | wc -l | cut -d" " -f1)
  if [[ $LC -gt 0 ]]; then
    echo "FAIL: Faulty "$LC" err verilog file tmp_yosys/${input}.err"
    let fail++
    fail_list+=" "$base
    continue
  fi
  if [ $? -eq 0 ]; then
    echo "Successfully created verilog from graph ${input}"
  else
    echo ${YOSYS} -g${base} -h -d
    echo "FAIL: verilog generation terminated with an error (testcase ${input})"
    let fail++
    fail_list+=" "$base
    continue
  fi
  $(cat tmp_yosys/*.v >tmp_yosys/all_${base}.v)

  if [[ $input =~ "nocheck_" ]]; then
    LC=$(wc -l tmp_yosys/all_${base}.v | cut -d" " -f1)
    echo "Skipping check for "$base" LC:"$LC
    if [[ $LC -lt 2 ]]; then
      echo "FAIL: Generated verilog file tmp_yosys/all_${base}.v is too small"
      let fail++
      fail_list+=" "$base
    fi
  else
    ${LGCHECK} --implementation=tmp_yosys/all_${base}.v --reference=${full_input} --top=${base}
    if [ $? -eq 0 ]; then
      echo "Successfully matched generated verilog with original verilog (${full_input})"
    else
      echo "FAIL: circuits are not equivalent (${full_input})"
      let fail++
      fail_list+=" "$base
    fi
  fi

  let pass++
done

if [ $fail -eq 0 ]; then
  echo "SUCCESS: pass:${pass} tests without errors"
else
  echo "FAIL: ${pass} tests passed but ${fail} failed verification: ${fail_list}"
fi


