#!/bin/bash
rm -rf ./lgdb

pts='trivial_bitwidth ssa_rhs function_call tuple ssa_nested_if ssa_if nested_if'
# pts='ssa_rhs'
# pts='tuple'
# pts='trivial_bitwidth'
# pts='ssa_no_else_if'
# pts='function_call'

LGSHELL=./bazel-bin/main/lgshell

if [ ! -f $LGSHELL ]; then
  if [ -f ./main/lgshell ]; then
    LGSHELL=./main/lgshell
    echo "lgshell is in $(pwd)"
  else
    echo "ERROR: could not find lgshell binary in $(pwd)";
  fi
fi

for pt in $pts
do
  echo "Pattern:${pt}.cfg"
  echo "Pattern:${pt}.cfg"
  echo "Pattern:${pt}.cfg"
  echo ""
  echo "---------------------------------------------------"
  echo "CFG -> LNAST -> Graphviz Test"
  echo "---------------------------------------------------"
  echo ""
  
  if [! -f inou/cfg/tests/${pt}.cfg]; then
    echo "ERROR: could not find ${pt}.cfg in /inou/cfg/tests"
    exit !
  fi

  ln -s inou/cfg/tests/${pt}.cfg;

  ${LGSHELL} "inou.graphviz.fromlnast files:${pt}.cfg"

  if [ -f ${pt}.lnast.dot ]; then
    echo "Successfully create a lnast from ${pt}.cfg"
  else
    echo "ERROR: Pyrope compiler failed: LNAST generation, testcase: ${pt}.cfg"
    exit 1
  fi
  
  cat ${pt}.lnast.dot | sort -n > lnast.nodes
  cat ./inou/cfg/tests/dot_gld/${pt}.lnast.dot.gld | sort -n > lnast.nodes.gld

  diff lnast.nodes lnast.nodes.gld
  exit_code=$? 

  if [[ $exit_code == 0 ]]; then
    echo "Successfully match the golden LNAST, testcase: ${pt}.cfg"
  else 
    echo "ERROR: Pyrope compiler failed: generated LNAST doesn't match the golden, testcase: ${pt}.cfg"
    exit 1
  fi
  


  echo ""
  echo ""
  echo ""
  echo "===================================================="
  echo "Pyrope Full Compilation"  
  echo "===================================================="

  echo "----------------------------------------------------"
  echo "CFG -> LNAST -> LGraph"  
  echo "----------------------------------------------------"
  
  ${LGSHELL} "inou.lnast_dfg.tolg files:${pt}.cfg"

  if [ $? -ne 0 ]; then
    echo "ERROR: Pyrope compiler failed: LNAST -> LGraph, testcase: ${pt}.cfg"
    exit 1
  fi

  echo ""
  echo ""
  echo ""
  echo "----------------------------------------------------"
  echo "Tuple Chain Resolve(LGraph)"  
  echo "----------------------------------------------------"
  ${LGSHELL} "lgraph.open name:${pt} |> inou.lnast_dfg.resolve_tuples"
  if [ $? -eq 0 ]; then
    echo "Successfully resolve the tuple chain: ${pt}.cfg"
  else
    echo "ERROR: Pyrope compiler failed: resolve tuples, testcase: ${pt}.cfg"
    exit 1
  fi


  echo ""
  echo ""
  echo ""
  echo "----------------------------------------------------"
  echo "Bitwidth Optimization(LGraph)"  
  echo "----------------------------------------------------"

  ${LGSHELL} "lgraph.open name:${pt} |> pass.bitwidth"
  if [ $? -eq 0 ]; then
    echo "Successfully optimize design bitwidth: ${pt}.v"
  else
    echo "ERROR: Pyrope compiler failed: bitwidth optimization, testcase: ${pt}.cfg"
    exit 1
  fi


  echo ""
  echo ""
  echo ""
  echo "----------------------------------------------------"
  echo "Dead Code Elimination(LGraph)"  
  echo "----------------------------------------------------"
  echo "Todo ..."


  echo ""
  echo ""
  echo ""
  echo "----------------------------------------------------"
  echo "LGraph -> Verilog"  
  echo "----------------------------------------------------"

  ${LGSHELL} "lgraph.open name:${pt} |> inou.yosys.fromlg"
  if [ $? -eq 0 ] && [ -f ${pt}.v ]; then
    echo "Successfully generate Verilog: ${pt}.v"
  else
    echo "ERROR: Pyrope compiler failed: verilog generation, testcase: ${pt}.cfg"
    exit 1
  fi


  echo ""
  echo ""
  echo ""
  echo "----------------------------------------------------"
  echo "Logic Equivalence Check"  
  echo "----------------------------------------------------"
  echo "Todo ..."



  rm -f ${pt}.cfg
  rm -f lnast.dot
  rm -f lnast.dot.gld
  rm -f lnast.nodes
  rm -f lnast.nodes.gld
  rm -f *.v
done



