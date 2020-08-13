#!/bin/bash
rm -rf ./lgdb
rm -rf ./lgdb2

pts='trivial2 trivial3 logic_bitwise_op_gld reduce common_sub operators loop_in_lg loop_in_lg2 mux mux2 trivial kogg_stone_64 simple_add trivial_and assigns compare simple_flop latch' # trivial1 mux latch add'
#TO ADD LIST, but have bugs:
#pick -- pick op not yet implemented in lnast2lg
#simple_flop, shift, cse_basic -- problems arise with flops somewhere??
#add -- sign isn't working yet
#arith -- same problem with minus, can't do %
#compare2 -- lnast2lg doesn't yet support range/bit_sel/etc. for pick nodes
#trivial2 -- subgraphs broke due to inp_edges going into subgraph
#consts -- don't have join->concat implemented yet
#submodule
#satsmall, satlarge -- mult not supported in bitwidth pass
#long_gcd -- lnast2lg does not handle ___ variables being used away from declaration

LGSHELL=./bazel-bin/main/lgshell
LGCHECK=./inou/yosys/lgcheck

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
    rm -rf ./lgdb
    rm -rf ./lgdb2

    echo "----------------------------------------------------"
    echo "Verilog -> LGraph -> LNAST -> LGraph"
    echo "----------------------------------------------------"
    ${LGSHELL} "inou.yosys.tolg files:inou/yosys/tests/${pt}.v top:${pt} |> pass.cprop |> pass.cprop |> inou.graphviz.from |> pass.lgraph_to_lnast |> lnast.dump |> inou.lnast_dfg.tolg path:lgdb2"
    if [ $? -eq 0 ]; then
      echo "Successfully created the inital LGraph using Yosys: ${pt}.v"
    else
      echo "ERROR: Verilog -> LGraph failed... testcase: ${pt}.v"
      exit 1
    fi
    mv ${pt}.dot ${pt}.origlg.dot

    echo ""
    echo "----------------------------------------------------"
    echo "LGraph Optimization"
    echo "----------------------------------------------------"
    ${LGSHELL} "lgraph.match path:lgdb2 |> pass.cprop |> pass.bitwidth |> pass.cprop |> pass.bitwidth |> pass.cprop |> pass.bitwidth |> pass.bitwidth"
    if [ $? -eq 0 ]; then
      echo "Successfully optimize design on new lg: ${pt}.v"
    else
      echo "ERROR: Failed to optimize design on new lg, testcase: ${pt}.v"
      exit 1
    fi
    ${LGSHELL} "lgraph.match path:lgdb2 |> inou.graphviz.from verbose:false"
    mv ${pt}.dot ${pt}.newlg.dot

    echo ""
    echo "----------------------------------------------------"
    echo "LGraph -> Verilog"
    echo "----------------------------------------------------"

    ${LGSHELL} "lgraph.match path:lgdb2 |> inou.yosys.fromlg"
    if [ $? -eq 0 ] && [ -f ${pt}.v ]; then
      echo "Successfully generate Verilog: ${pt}.v"
      rm -f  yosys_script.*
    else
      echo "ERROR: Yosys failed: verilog generation, testcase: ${pt}.v"
      exit 1
    fi

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
    ${LGSHELL} "lgraph.match path:lgdb2 |> inou.graphviz.from verbose:false"

done #end of for
