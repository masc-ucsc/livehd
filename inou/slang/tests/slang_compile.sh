#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

echo "slang_compile.sh running in $(pwd)"

LGSHELL=./bazel-bin/main/lgshell

if [ ! -x $LGSHELL ]; then
  if [ -x ./main/lgshell ]; then
    LGSHELL=./main/lgshell
    echo "lgshell is in $(pwd)"
  else
    echo "FAILED: slang_compile.sh could not find lgshell binary in $(pwd)";
    exit 1
  fi
fi

rm -rf ./logs

inputs=inou/slang/tests/verilog/*.v
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
  echo "slang_compile.sh inputs: ${inputs}"
fi

pass=0
fail=0
fail_list=""
pass_list=""
for full_input in ${inputs}
do
  STARTTIME=$SECONDS
  #echo "starting test "${input}" at "$(/usr/bin/date)
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

  rm -rf tmp_slang
  mkdir -p tmp_slang

  lnast_file="tmp_slang/${base}.lnast"
  cmd_parse="inou.slang files:${full_input} |> pass.lnastfmt |> pass.upass constprop:1 verifier:0 |> pass.lnastfmt |> lnast.dump file:${lnast_file}"
  echo "${cmd_parse}" | ${LGSHELL} -q >tmp_slang/${input}.log 2>tmp_slang/${input}.err
  echo "CMD: ${cmd_parse}"
  if [ $? -eq 0 ]; then
    echo "Successfully created LNAST from ${input}"
  else
    echo "FAIL: slang LNAST parsing/upass terminated with an error (testcase ${input})"
    cat tmp_slang/${input}.log
    cat tmp_slang/${input}.err
    ((fail++))
    fail_list+=" "$base
    continue
  fi
  LC=$(grep -iv Warning tmp_slang/${input}.err | grep -v perf_event | grep -v "recommended to use " | grep -v "IPC=" | grep -v "uPass - verifier aggregate cassert counts:" | wc -l | cut -d" " -f1)
  if [[ $LC -gt 0 ]]; then
    echo "FAIL: Faulty $LC err slang file tmp_slang/${input}.err"
    ((fail++))
    fail_list+=" "$base
    continue
  fi
  LC=$(grep -i signal tmp_slang/${input}.log | wc -l | cut -d" " -f1)
  if [[ $LC -gt 0 ]]; then
    echo "FAIL: Faulty $LC log slang file tmp_slang/${input}.log"
    ((fail++))
    fail_list+=" "$base
    continue
  fi

  if [ ! -s "${lnast_file}" ]; then
    echo "FAIL: LNAST dump ${lnast_file} is empty or missing"
    ((fail++))
    fail_list+=" "$base
    continue
  fi

  cmd_read="lnast.read file:${lnast_file} |> pass.lnastfmt |> pass.upass constprop:1 verifier:0 |> pass.lnastfmt"
  echo "${cmd_read}" | ${LGSHELL} -q >tmp_slang/${input}.reload.log 2>tmp_slang/${input}.reload.err
  echo "CMD: ${cmd_read}"
  if [ $? -eq 0 ]; then
    echo "Successfully reloaded LNAST from ${input}"
  else
    echo "FAIL: LNAST read/upass terminated with an error (testcase ${input})"
    cat tmp_slang/${input}.reload.log
    cat tmp_slang/${input}.reload.err
    ((fail++))
    fail_list+=" "$base
    continue
  fi

  LC=$(grep -iv Warning tmp_slang/${input}.reload.err | grep -v perf_event | grep -v "recommended to use " | grep -v "IPC=" | grep -v "uPass - verifier aggregate cassert counts:" | wc -l | cut -d" " -f1)
  if [[ $LC -gt 0 ]]; then
    echo "FAIL: Faulty $LC err slang reload file tmp_slang/${input}.reload.err"
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

if [ $FAIL -eq 0 ]; then
  echo "SUCCESS: pass:${pass} tests without errors"
  exit 0
else
  echo "FAIL: ${pass} tests passed: ${pass_list}"
  echo "FAIL: ${fail} tests failed: ${fail_list}"
  exit 1
fi
