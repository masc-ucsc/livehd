#!/bin/bash
# Smoke test for the standalone `slang` binary (inou/slang/tests/slang.cpp): it
# runs slang_main on a trivial design and must parse + lower it to LNAST cleanly
# (exit 0). The old --ast-json JSON dump it used to grep is gone in the slang
# v11 driver port (todo/ 1s subtask B); the real SV->LNAST->round-trip coverage
# lives in the slang_compile-* targets.

SLANG=./bazel-bin/inou/slang/slang

if [ ! -f $SLANG ]; then
  if [ -f ./inou/slang/slang ]; then
    SLANG=./inou/slang/slang
    echo "slang is in $(pwd)"
  else
    echo "ERROR: could not find slang binary in $(pwd)";
    exit -2
  fi
fi

for file in inou/yosys/tests/trivial.v
do
  ${SLANG} --quiet ${file}
  ret_val=$?
  if [ $ret_val -ne 0 ]; then
    echo "ERROR: could not direct execute slang with file:${file}!"
    exit $ret_val
  fi
done

exit 0
