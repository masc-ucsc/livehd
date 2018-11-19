#!/bin/bash

LGSHELL=./bazel-bin/main/lgshell

if [ ! -x $LGSHELL ]; then
  if [ -x ./main/lgshell ]; then
    LGSHELL=./main/lgshell
    echo "lgshell is in $(pwd)"
  else
    echo "FAILED: pyrope_test.sh could not find lgshell binary in $(pwd)";
  fi
fi


for tst in trivial
do
  if [ ! -f inou/yosys/tests/${tst}.v ]; then
    echo "pyrope_test.sh could not access verilog to run ${tst} test"
    exit 3
  fi
  if [ ! -f inou/pyrope/tests/${tst}.prp ]; then
    echo "pyrope_test.sh could not access pyrope to run ${tst} test"
    exit 3
  fi

  echo "inou.yosys.tolg files:./inou/yosys/tests/${tst}.v |> inou.pyrope.fromlg odir:tmp" |  ${LGSHELL}
  if [ $? -ne 0 ]; then
    echo "pyrope_test.sh failed to run ${tst} test"
    exit 3
  fi
  diff -bBdNrw inou/pyrope/tests/${tst}.prp tmp/${tst}.prp
  if [ $? -ne 0 ]; then
    echo "pyrope_test.sh generated code inou/pyrope/tests/${tst}.prp differs for tmp/${tst}.prp test"
    exit 2
  fi
done


