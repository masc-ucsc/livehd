#!/bin/bash
rm -rf ./lgdb

pts='loop_in_lg loop_in_lg2 gcd_small async mux flop assigns pick compare2 compare gates'

LGSHELL=./bazel-bin/main/lgshell
LGCHECK=./inou/yosys/lgcheck

if [ ! -f $LGSHELL ]; then
    if [ -f ./main/lgshell ]; then
        LGSHELL=./main/lgshell
        echo "lgshell is in $(pwd)"
    else
        echo "ERROR: could not find lgshell binary in $(pwd)";
        exit 1
    fi
fi

if [ ! -d "../firrtl" ]; then
  echo "The FIRRTL directory not found (must be in same directory as LiveHD)"
  exit 1
fi

for pt in $pts
do
    echo ""
    echo ""
    echo ""
    echo "===================================================="
    echo "Verify LNAST -> FIRRTL"
    echo "===================================================="


    echo "----------------------------------------------------"
    echo "Verilog -> LGraph"
    echo "----------------------------------------------------"

    ${LGSHELL} "inou.yosys.tolg files:inou/yosys/tests/${pt}.v"
    if [ $? -eq 0 ]; then
      echo "Successfully translated Verilog to LGraph: ${pt}"
    else
      echo "ERROR: Verilog -> LG failed... testcase: ${pt}"
      exit 1
    fi

    echo ""
    echo ""
    echo ""
    echo "----------------------------------------------------"
    echo "LGraph -> LNAST -> FIRRTL (Proto)"
    echo "----------------------------------------------------"

    ${LGSHELL} "lgraph.open name:${pt} |> pass.lgraph_to_lnast |> inou.firrtl.tofirrtl"
    if [ $? -eq 0 ]; then
      echo "Successfully generated FIRRTL (Proto): ${pt}"
    else
      echo "ERROR: Failed translating from LG -> LN -> FIR: ${pt}"
      exit 1
    fi

    echo ""
    echo ""
    echo ""
    echo "----------------------------------------------------"
    echo "FIRRTL Compiler: FIRRTL (Proto) -> Verilog"
    echo "----------------------------------------------------"

    cd ../firrtl/.
    ./utils/bin/firrtl -i ../livehd/${pt}.pb -X verilog
    if [ $? -eq 0 ]; then
      echo "Successfully generated Verilog in FIRRTL compiler: ${pt}"
    else
      echo "ERROR: FIRRTL compiler Verilog generation failed: ${pt}"
      exit 1
    fi
    cd ../livehd/.

    echo ""
    echo ""
    echo ""
    echo "----------------------------------------------------"
    echo "Logic Equivalence Check"
    echo "----------------------------------------------------"

    ${LGCHECK} --implementation=../firrtl/${pt}.v --reference=./inou/yosys/tests/${pt}.v

    if [ $? -eq 0 ]; then
      echo "Successfully pass logic equivilence check!"
    else
      echo "FAIL: "${pt}".v !== "${pt}".gld.v"
      exit 1
    fi
done #end of for
