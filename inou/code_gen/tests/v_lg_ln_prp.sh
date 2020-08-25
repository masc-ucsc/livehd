#!/bin/bash
rm -rf ./lgdb
rm -rf ./lgdb2
rm -rf ./*.dot
rm -rf ./*.pdf
pts='mem_reset'


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
    rm -rf ./lgdb
    rm -rf ./lgdb2

    echo "----------------------------------------------------"
    echo "Verilog -> LGraph"
    echo "----------------------------------------------------"
    ${LGSHELL} "inou.yosys.tolg files:inou/yosys/tests/${pt}.v top:${pt} |> pass.cprop |> pass.cprop |> inou.graphviz.from "
    if [ $? -eq 0 ]; then
      echo "Successfully created the inital LGraph using Yosys: ${pt}.v"
    else
      echo "ERROR: Verilog -> LGraph failed... testcase: ${pt}.v"
      exit 1
    fi
    ##mv ${pt}.dot ${pt}.origlg.dot
    ##mv ${pt}.lnast.dot 
    dot -Tpdf -o ${pt}.dot.pdf ${pt}.dot

    echo ""
    echo "----------------------------------------------------"
    echo "LGraph to LNAST to code_gen(prp)"
    echo "----------------------------------------------------"
    ${LGSHELL} "lgraph.match |> pass.lnast_fromlg |> lnast.dump |> inou.graphviz.from |> inou.code_gen.prp"
    if [ $? -eq 0 ]; then
      echo "Successfully created the LNAST for: ${pt}.v"
    else
      echo "ERROR: Failed to optimize design on new lg, testcase: ${pt}.v"
      exit 1
    fi
    dot -Tpdf -o ${pt}.lnast.dot.pdf ${pt}.lnast.dot

done #end of for
