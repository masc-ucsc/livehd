#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

echo "pyrope_compile.sh running in "$(pwd)

LGSHELL=./bazel-bin/main/lgshell

if [ ! -x $LGSHELL ]; then
  if [ -x ./main/lgshell ]; then
    LGSHELL=./main/lgshell
    echo "lgshell is in $(pwd)"
  else
    echo "FAILED: pyrope_compile.sh could not find lgshell binary in $(pwd)"
    exit 3
  fi
fi

LGCHECK=./inou/yosys/lgcheck

PRP_DIR=inou/pyrope/tests
GLD_DIR=inou/pyrope/tests/verilog_gld

inputs=""
if [ "$1" != "" ]; then
  while [ "$1" != "" ]; do
    arg=$1
    shift
    base=$(basename "${arg}")
    base=${base%.prp}
    base=${base%.gld.v}
    base=${base%.v}
    if [ -f "${PRP_DIR}/${base}.prp" ]; then
      inputs+=" ${PRP_DIR}/${base}.prp"
    else
      echo "FAIL: could not find pyrope source for ${arg} (looked for ${PRP_DIR}/${base}.prp)"
      exit 3
    fi
  done
  echo "pyrope_compile.sh inputs: ${inputs}"
else
  inputs=${PRP_DIR}/*.prp
fi

pass=0
fail=0
skip=0
fail_list=""
pass_list=""
skip_list=""

rm -rf tmp_pyrope_mix
mkdir -p tmp_pyrope_mix

for full_input in ${inputs}
do
  STARTTIME=$SECONDS
  input=$(basename "${full_input}")
  base=${input%.prp}

  if [ ! -f "${GLD_DIR}/${base}.gld.v" ]; then
    echo "Skipping ${base}: no golden ${GLD_DIR}/${base}.gld.v"
    skip_list+=" "$base
    ((skip++))
    continue
  fi

  echo "${full_input}"

  rm -rf lgdb_pyrope tmp_pyrope
  mkdir -p tmp_pyrope

  CMD="inou.pyrope path:lgdb_pyrope files:${full_input} |> pass.lnast_tolg |> pass.cprop |> pass.bitwidth |> inou.cgen.verilog odir:tmp_pyrope"
  echo "CMD: ${CMD}"
  echo "${CMD}" | ${LGSHELL} -q >tmp_pyrope/${input}.log 2>tmp_pyrope/${input}.err
  if [ $? -ne 0 ]; then
    echo "FAIL: pyrope compile terminated with an error (testcase ${input})"
    cat tmp_pyrope/${input}.log
    cat tmp_pyrope/${input}.err
    ((fail++))
    fail_list+=" "$base
    continue
  fi
  LC=$(grep -iv Warning tmp_pyrope/${input}.err | grep -v perf_event | grep -v "recommended to use " | grep -v "IPC=" | wc -l | cut -d" " -f1)
  if [[ $LC -gt 0 ]]; then
    echo "FAIL: Faulty $LC err pyrope file tmp_pyrope/${input}.err"
    cat tmp_pyrope/${input}.err
    ((fail++))
    fail_list+=" "$base
    continue
  fi

  if [ ! -s tmp_pyrope/${base}.v ]; then
    echo "FAIL: no verilog generated at tmp_pyrope/${base}.v"
    ((fail++))
    fail_list+=" "$base
    continue
  fi

  cat tmp_pyrope/*.v >tmp_pyrope_mix/all_${base}.v

  ${LGCHECK} --implementation tmp_pyrope_mix/all_${base}.v --reference ${GLD_DIR}/${base}.gld.v --top ${base}
  if [ $? -eq 0 ]; then
    echo "Successfully matched generated verilog with golden (${GLD_DIR}/${base}.gld.v)"
  else
    echo "FAIL: circuits are not equivalent (${full_input} vs ${GLD_DIR}/${base}.gld.v)"
    ((fail++))
    fail_list+=" "$base
    continue
  fi

  pass_list+=" "$base
  ((pass++))

  ENDTIME=$SECONDS
  echo "perf: takes $(($ENDTIME - $STARTTIME)) for top:"${base}
done

FAIL=$fail
for job in $(jobs -p)
do
  echo $job
  wait $job || let "FAIL+=1"
done

if [[ $skip -gt 0 ]]; then
  echo "SKIP: ${skip} tests skipped (no .gld.v): ${skip_list}"
fi

if [ $FAIL -eq 0 ]; then
  echo "SUCCESS: pass:${pass} tests without errors"
  exit 0
else
  echo "FAIL: ${pass} tests passed: ${pass_list}"
  echo "FAIL: ${fail} tests failed: ${fail_list}"
  exit 1
fi
