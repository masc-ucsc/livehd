#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Direct slang front-end (SV -> LNAST) via the lhd kernel:
#   1. `lhd compile --reader slang` runs inou.slang + lnastfmt + pass.upass
#      (constprop:1 verifier:false) and emits the post-upass LNAST both as the
#      binary `ln:` Forest dir and the textual `lnast-dump:` observable.
#   2. `lhd synth ln:` reloads the saved Forest and re-runs upass — the
#      serialization round-trip. NOTE: this is the binary hhds round-trip; the
#      old lgshell flow round-tripped through the *textual* lnast.dump/
#      lnast.read pair, which is no longer covered here (it remains an
#      lgshell-only feature).
# lhd checks the diag sink after every step, so the old stderr-grep error
# heuristics reduce to exit codes.

echo "slang_compile.sh running in $(pwd)"

LHD=./bazel-bin/lhd/lhd

if [ ! -x $LHD ]; then
  if [ -x ./lhd/lhd ]; then
    LHD=./lhd/lhd
    echo "lhd is in $(pwd)"
  else
    echo "FAILED: slang_compile.sh could not find the lhd binary in $(pwd)";
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

  ln_dir="tmp_slang/${base}_ln"
  dump_dir="tmp_slang/${base}_dump"
  ${LHD} compile ${full_input} --reader slang \
    --emit-dir ln:${ln_dir}/ --emit-dir lnast-dump:${dump_dir}/ \
    --workdir tmp_slang/${base} -q --result-json tmp_slang/${input}.result.json \
    >tmp_slang/${input}.log 2>tmp_slang/${input}.err
  if [ $? -eq 0 ]; then
    echo "Successfully created LNAST from ${input}"
  else
    echo "FAIL: slang LNAST parsing/upass terminated with an error (testcase ${input})"
    cat tmp_slang/${input}.result.json 2>/dev/null
    cat tmp_slang/${input}.err
    ((fail++))
    fail_list+=" "$base
    continue
  fi

  shopt -s nullglob
  dump_files=(${dump_dir}/*.lnast)
  shopt -u nullglob
  if [ ${#dump_files[@]} -eq 0 ] || [ ! -s "${dump_files[0]}" ]; then
    echo "FAIL: LNAST dump in ${dump_dir} is empty or missing"
    ((fail++))
    fail_list+=" "$base
    continue
  fi

  # Reload the saved ln: Forest and re-run upass (serialization round-trip).
  ${LHD} synth ln:${ln_dir}/ \
    --workdir tmp_slang/${base}_reload -q --result-json tmp_slang/${input}.reload.json \
    >tmp_slang/${input}.reload.log 2>tmp_slang/${input}.reload.err
  if [ $? -eq 0 ]; then
    echo "Successfully reloaded LNAST from ${input}"
  else
    echo "FAIL: ln: reload/upass terminated with an error (testcase ${input})"
    cat tmp_slang/${input}.reload.json 2>/dev/null
    cat tmp_slang/${input}.reload.err
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
