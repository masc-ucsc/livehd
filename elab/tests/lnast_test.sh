#!/bin/bash

# pts='function_call tuple ssa_nested_if ssa_if nested_if'
#pts='tuple'
pts='trivial_bitwidth'
# pts='ssa_no_else_if'
# pts='function_call'

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

  if [ -f ${pt}.lnast.dot ]; then
    echo "Successfully create a lnast from ${pt}.cfg"
  else
    echo "FAIL: LNAST generation terminated with an error, testcase: ${pt}.cfg"
    exit 1
  fi
  
  cat ${pt}.lnast.dot | sort -n > lnast.nodes
  cat ./inou/cfg/tests/dot_gld/${pt}.lnast.dot.gld | sort -n > lnast.nodes.gld

  diff lnast.nodes lnast.nodes.gld
  exit_code=$? 

  if [[ $exit_code == 0 ]]; then
    echo "Successfully match the golden LNAST, testcase: ${pt}.cfg"
  else 
    echo "FAIL: generated LNAST doesn't match the golden target, testcase: ${pt}.cfg"
    exit 1
  fi
  
  rm -f ${pt}.cfg
  rm -f lnast.dot
  rm -f lnast.dot.gld
  rm -f lnast.nodes
  rm -f lnast.nodes.gld
done


