#!/bin/bash

pts='lnast_utest'
# pts='counter'

LGSHELL=./bazel-bin/main/lgshell


if [ ! -f $LGSHELL ]; then
  if [ -f ./main/lgshell ]; then
    LGSHELL=./main/lgshell
    echo "lgshell is in $(pwd)"
  else
    echo "FAILED: could not find lgshell binary in $(pwd)";
  fi
fi



for pt in $pts
do
  echo "Pattern:${pt}.cfg"
  echo "Pattern:${pt}.cfg"
  echo "Pattern:${pt}.cfg"
  echo ""
  echo "CFG to LNAST to Graphviz Flow"
  echo ""

  ln -s inou/cfg/tests/${pt}.cfg;

  ${LGSHELL} "inou.graphviz.fromlnast files:${pt}.cfg"

  if [ -f ${pt}.cfg.lnast.dot ]; then
    echo "Successfully created lnast from ${pt}.cfg"
  else
    echo "FAIL: LNAST generation terminated with an error, testcase: ${pt}.cfg"
    exit 1
  fi
  
  rm -f ${pt}.cfg
done



