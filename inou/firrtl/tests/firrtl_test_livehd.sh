#!/bin/bash
rm -rf ./lgdb
pts='logic_bitwise_op_gld common_sub
     operators mux mux2 trivial simple_add
     trivial_and assigns compare trivial1'

#pts='mux mux2 assigns pick gates'
#Working with local versions: long_gcd, flop
#Failing:
#  fails because some IO is removed due to DCE:
#     cse_basic
#  need to figure out which is actually "top":
#     hierarchy
#  nodes that have no inputs
#     kogg_stone_64(join)
#     long_BTBsa(join)
#  Use of System-Verilog causes problems:
#     long_iwls_adder(seems like yosys issue)
#     trivial3
#
# consts loop_in_lg loop_in_lg2 compare2 gcd_small async submodule


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

for pt in $pts
do
    rm -rf ./lgdb
    if [ -f ${pt}.v ]; then rm ${pt}.v; fi

    echo ""
    echo "===================================================="
    echo "Verify FIRRTL Interface"
    echo "===================================================="

    echo "----------------------------------------------------"
    echo "Verilog -> LGraph -> LNAST -> FIRRTL (Proto)"
    echo "----------------------------------------------------"
    ${LGSHELL} "inou.yosys.tolg files:inou/yosys/tests/${pt}.v top:${pt} |> pass.cprop |> pass.cprop |> inou.graphviz.from |> pass.lgraph_to_lnast bw_in_ln:false |> lnast.dump |> inou.firrtl.tofirrtl"
    if [ $? -eq 0 ]; then
      echo "Successfully generated FIRRTL (Proto): ${pt}"
    else
      echo "ERROR: Failed translating from LG -> LN -> FIR: ${pt}"
      exit 1
    fi
    mv ${pt}.dot ${pt}.origlg.dot

    echo ""
    echo "----------------------------------------------------"
    echo "FIRRTL (Proto) -> LNAST -> LGraph -> Verilog"
    echo "----------------------------------------------------"
    ${LGSHELL} "inou.firrtl.tolnast files:${pt}.pb |> lnast.dump |> pass.lnast_dfg |> pass.cprop |> pass.bitwidth |> pass.cprop |> pass.bitwidth |> pass.cprop |> pass.bitwidth |> pass.bitwidth |> inou.graphviz.from |> inou.yosys.fromlg"
    if [ $? -eq 0 ]; then
      echo "Successfully generated Verilog (Proto): ${pt}"
    else
      echo "ERROR: Failed translating from FIR -> LN -> LG -> VER: ${pt}"
      exit 1
    fi
    mv ${pt}.dot ${pt}.newlg.dot

    echo ""
    echo "----------------------------------------------------"
    echo "Logic Equivalence Check"
    echo "----------------------------------------------------"
    ${LGCHECK} --implementation=${pt}.v --reference=./inou/yosys/tests/${pt}.v

    if [ $? -eq 0 ]; then
      echo "Successfully pass logic equivilence check!"
    else
      echo "FAIL: "${pt}".v !== "${pt}".gld.v"
      exit 1
    fi

    rm -f ${pt}.pb
    rm -f ${pt}.v
    rm -f ${pt}.*dot
done #end of for
