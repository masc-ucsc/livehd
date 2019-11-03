#!/bin/bash
rm -rf ./lgdb
rm -f   yosys_srcipt.*
rm -f   *.v
rm -r  ./logs/*.v
rm -f  ./logs/*.dot
rm -f  ./logs/yosys_log/yosys_script.*

mkdir logs
mkdir -p logs/yosys_log

# TBD
# pts='test simple_weird fflop join_fadd'

# wait for hierarchical traversal
# pts='trivial2 unconnected hierarchy '

# SH:todo
# pts='compare2 consts cse_basic dce2 dce3 expression_00002
# graphtest kogg_stone_64 '
#pts='trivial1 trivial trivial2a trivial3 trivial_and 
#     dce1 gates trivial1 trivial_join compare compare2'

pts='compare compare2'
     
LGSHELL=./bazel-bin/main/lgshell
LGCHECK=./inou/yosys/lgcheck


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
  echo "Pattern:${pt}.v"
  echo "Pattern:${pt}.v"
  echo "Pattern:${pt}.v"
  echo ""
  echo "Mockturtle LUT Synthesis Flow"
  echo ""
  ${LGSHELL} "inou.yosys.tolg files:inou/yosys/tests/${pt}.v"
  ${LGSHELL} "lgraph.open name:${pt}          |> inou.graphviz.fromlg"
  ${LGSHELL} "lgraph.open name:${pt}          |> pass.mockturtle"
  ${LGSHELL} "lgraph.open name:${pt}_lutified |> inou.yosys.fromlg"
  ${LGSHELL} "lgraph.open name:${pt}_lutified |> inou.graphviz.fromlg"

  if [ $? -eq 0 ] && [ -f ${pt}_lutified.v ]; then
    echo "Successfully created lutified verilog:${pt}_lutified.v"
  else
    echo "FAIL: verilog generation terminated with an error, testcase: ${pt}.v"
    exit 1
  fi
  
  mv *.v ./logs
  mv *.dot ./logs
  mv yosys_script.* ./logs/yosys_log

  echo ""
  echo "Logic Equivalence Check"
  echo ""
  
  ${LGCHECK} -r./inou/yosys/tests/${pt}.v -i./logs/${pt}_lutified.v
  if [ $? -eq 0 ]; then
    echo "Successfully pass logic equivilence check!"
    echo "=========================================="
    echo "=========================================="
    echo ""
  else
    echo "FAIL: "$pt".v !== "$pt"_gld.v"
    exit 1
  fi

done



