#!/bin/bash


rm -rf ./lgdb 

#unsupported ShiftOp
#pts = params satlarge satsmall satpick shiftx shiftx_simple test simple_add simple_rf2 
#worth to try first
#pts = operators reduce unconnected wires
pts='trivial trivial2a trivial3'

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
  if [ ! -f ./inou/yosys/tests/${pt}.v ]; then
    echo "could not find ${pt}.v in ./inou/yosys/tests"
    exit 1
  fi

  echo ""
  echo "Verilog->LGraph->LGraph_Lutified"
  echo ""

  echo "inou.yosys.tolg files:./inou/yosys/tests/${pt}.v" | ${LGSHELL}
  echo "lgraph.open name:${pt} |> pass.mockturtle"        | ${LGSHELL}

  if [ $? -ne 0 ]; then
    echo "mockturtle.sh failed @ (${pt})"
    exit 3
  fi
done


echo ""
echo "LGraph_lutified->Verilog code generation"
echo ""

for pt in $pts
do
  echo "lgraph.open name:${pt}_lutified |> inou.yosys.fromlg" | ${LGSHELL}
  if [ $? -eq 0 ] && [ -f ${pt}_lutified.v ]; then
    echo "Successfully created verilog:${pt}_lutified.v"
  else
    echo "FAIL: verilog generation terminated with an error, testcase: ${pt}.v"
    exit 1
  fi
done


echo ""
echo "Logic Equivalence Check"
echo ""

for pt in $pts
do
  ./inou/yosys/lgcheck -r"$pt"_lutified.v -i./inou/yosys/tests/"$pt".v


  if [ $? -eq 0 ]; then
    echo "Successfully pass logic equivilence check!"
  else
    echo "FAIL: "$pt".v !== "$pt"_lutified.v"
    exit 1
  fi
done



