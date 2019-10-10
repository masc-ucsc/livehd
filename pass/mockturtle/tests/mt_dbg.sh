#!/bin/bash
rm -rf ./lgdb
rm -f   yosys_srcipt.*
rm -f   *.v

# pts='trivial trivial2a trivial1'
pts='trivial'
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

  if [ $? -eq 0 ] && [ -f ${pt}_lutified.v ]; then
    echo "Successfully created lutified verilog:${pt}_lutified.v"
  else
    echo "FAIL: verilog generation terminated with an error, testcase: ${pt}.v"
    exit 1
  fi
  

  echo ""
  echo "Logic Equivalence Check"
  echo ""
  
  ${LGCHECK} -r./inou/yosys/tests/${pt}.v -i${pt}_lutified.v
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




