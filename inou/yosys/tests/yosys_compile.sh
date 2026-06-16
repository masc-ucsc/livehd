#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Verilog round-trip via the lhd kernel: each test compiles
# verilog -> LGraph (yosys-verilog reader) -> cprop (O1) -> cgen verilog,
# then LECs the generated netlist against the original with `lhd lec --set lec.solver=lgyosys`
# (inou/yosys/lgcheck underneath). One `lhd compile` replaces the old
# tolg|>lgraph.save + lgraph.match|>cprop|>cgen lgshell pipelines; the
# stderr-grep heuristics are gone because lhd checks the diag sink after
# every step and reflects it in the exit code.

echo "yosys_compile.sh running in "$(pwd)

LHD=./bazel-bin/lhd/lhd

if [ ! -x $LHD ]; then
  if [ -x ./lhd/lhd ]; then
    LHD=./lhd/lhd
    echo "lhd is in $(pwd)"
  else
    echo "FAILED: yosys_compile.sh could not find the lhd binary in $(pwd)";
    exit 1
  fi
fi

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
  echo "yosys_compile.sh inputs: ${inputs}"
fi

pass=0
fail=0
fail_list=""
pass_list=""
rm -rf tmp_yosys_mix
mkdir -p tmp_yosys_mix
for full_input in ${inputs}
do
  STARTTIME=$SECONDS
  input=$(basename ${full_input})
  echo ${full_input}
  base=${input%.*}

  if [[ $input =~ "long_" ]]; then
    if [[ $long == "" ]]; then
      echo "Skipping long test for "$base
      continue
    fi
    base=${base:5}
  else
    if [[ $long == "true" && $fixme != "true" ]]; then
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

  rm -rf tmp_yosys
  mkdir -p tmp_yosys

  # verilog -> lg -> cprop -> verilog, one stateless action. The per-step
  # logs (yosys chatter included) land under the --workdir.
  ${LHD} compile ${full_input} --reader yosys-verilog --top ${base} --recipe O1 \
    --emit verilog:tmp_yosys_mix/all_${base}.v \
    --workdir tmp_yosys/${base} -q --result-json tmp_yosys/${input}.result.json \
    >tmp_yosys/${input}.log 2>tmp_yosys/${input}.err
  compile_rc=$?
  if [ $compile_rc -eq 0 ]; then
    echo "Successfully created verilog from ${input}"
  else
    echo "FAIL: lhd compile terminated with an error rc=$compile_rc (testcase ${input})"
    cat tmp_yosys/${input}.result.json 2>/dev/null
    cat tmp_yosys/${input}.err
    ((fail++))
    fail_list+=" "$base
    continue
  fi
  if [ ! -s tmp_yosys_mix/all_${base}.v ]; then
    echo "FAIL: generated verilog tmp_yosys_mix/all_${base}.v is empty (testcase ${input})"
    ((fail++))
    fail_list+=" "$base
    continue
  fi

  if [[ $input =~ "nocheck_" ]]; then
    LC=$(wc -l < tmp_yosys_mix/all_${base}.v | tr -d ' ')
    echo "Skipping check for $base LC:"$LC
    if [[ $LC -lt 2 ]]; then
      echo "FAIL: Generated verilog file tmp_yosys_mix/all_${base}.v is too small"
      ((fail++))
      fail_list+=" "$base
      continue
    fi
  else
    ${LHD} lec --set lec.solver=lgyosys --impl verilog:tmp_yosys_mix/all_${base}.v --ref verilog:${full_input} \
      --top ${base} --workdir tmp_yosys/${base}_check -q \
      --result-json tmp_yosys/${input}.check.json >/dev/null 2>&1
    if [ $? -eq 0 ]; then
      echo "Successfully matched generated verilog with original verilog (${full_input})"
    else
      echo "FAIL: circuits are not equivalent (${full_input})"
      cat tmp_yosys/${input}.check.json 2>/dev/null
      ((fail++))
      fail_list+=" "$base
      continue
    fi
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

if [ $FAIL -eq 0 ]; then
  echo "SUCCESS: pass:${pass} tests without errors"
  exit 0
else
  echo "FAIL: ${pass} tests passed: ${pass_list}"
  echo "FAIL: ${fail} tests failed: ${fail_list}"
  exit 1
fi
