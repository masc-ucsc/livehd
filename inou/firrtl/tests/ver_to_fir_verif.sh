#!/bin/bash
rm -rf ./lgdb

pts='gates consts loop_in_lg loop_in_lg2 compare2 gcd_small async mux mux2 assigns pick gates'
#Working with local versions: long_gcd, flop
#Failing:
#  fails because some IO is removed due to DCE:
#     cse_basic
#  need to figure out which is actually "top":
#     hierarchy
#  nodes that have no inputs
#     kogg_stone_64(join)
#     long_BTBsa(join)
#  need to lookg more into
#     long_iwls_adder(seems like yosys issue)
#


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
    rm -rf ./lgdb
    echo ""
    echo ""
    echo ""
    echo "===================================================="
    echo "Verify LNAST -> FIRRTL"
    echo "===================================================="

    echo ""
    echo ""
    echo ""
    echo "----------------------------------------------------"
    echo "Verilog -> LGraph -> LNAST -> FIRRTL (Proto)"
    echo "----------------------------------------------------"

    ${LGSHELL} "inou.yosys.tolg files:inou/yosys/tests/${pt}.v |> pass.lgraph_to_lnast bw_in_ln:false |> inou.firrtl.tofirrtl"
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
    ./utils/bin/firrtl -i ../livehd/${pt}.pb -X mverilog
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
