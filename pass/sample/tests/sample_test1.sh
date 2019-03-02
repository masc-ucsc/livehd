#!/bin/sh

echo "Sample cmd line test that should run at top level and bazel test"

LGSHELL=./bazel-bin/main/lgshell

if [ ! -f $LGSHELL ]; then
  if [ -f ./main/lgshell ]; then
    LGSHELL=./main/lgshell
    echo "lgshell is in $(pwd)"
  else
    echo "FAILED: could not find lgshell binary in $(pwd)";
  fi
fi

echo "Parsing all the some specific Verilog files"
echo "files path:inou/yosys/tests match:\".*_square.v$\" |> inou.yosys.tolg" | $LGSHELL

# If many files are needed sample inou/yosys/tests/*.v and/or projects/boom/*

echo "Sample pass"
regresult=$(echo "lgraph.open name:nocheck_iwls_square |> pass.sample" | $LGSHELL | grep "Pass: cells ")
if [ "${regresult}" ]; then
  echo "PASS: found ${regresult}"
  exit 0
fi

echo "FAIL: missing Pass cells:, found ${regresult}"
exit -3

