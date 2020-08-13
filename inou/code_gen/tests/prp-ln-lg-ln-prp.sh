#!/bin/bash
rm -rf ./lgdb
rm -rf ./lgdb2
rm -rf ./*.dot*

pts='logic' # tuple_if'
folder='cfg' #could be pyrope

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
    echo ""
    echo ""
    echo ""
    echo "===================================================="
    echo "Compilation to get stable LGraph"
    echo "===================================================="

    echo "----------------------------------------------------"
    echo "Pyrope -> LNAST -> LGraph"
    echo "----------------------------------------------------"

    ${LGSHELL} "inou.pyrope files:inou/${folder}/tests/${pt}.prp |> inou.lnast_dfg.tolg"
    if [ $? -eq 0 ]; then
      echo "Successfully create the inital LGraph with tuples: ${pt}.cfg"
    else
      echo "ERROR: Pyrope compiler failed: LNAST -> LGraph, testcase: ${pt}.cfg"
      exit 1
    fi

    ${LGSHELL} "lgraph.open name:${pt} |> inou.graphviz.from verbose:false"
    mv ${pt}.dot ${pt}.no_bits.tuple.reduced_or.dot
    dot -Tpdf -o ${pt}.no_bits.tuple.reduced_or.dot.pdf ${pt}.no_bits.tuple.reduced_or.dot

    echo ""
    echo ""
    echo ""
    echo "----------------------------------------------------"
    echo "cprop and bitwidth"
    echo "----------------------------------------------------"

    ${LGSHELL} "lgraph.open name:${pt} |> pass.cprop |> pass.bitwidth |> pass.cprop |> pass.bitwidth"
    if [ $? -eq 0 ]; then
      echo "Successfully optimize design bitwidth: ${pt}.v"
    else
      echo "ERROR: Pyrope compiler failed: bitwidth optimization, testcase: ${pt}.cfg"
      exit 1
    fi

    ${LGSHELL} "lgraph.open name:${pt} |> inou.graphviz.from verbose:false"
    mv ${pt}.dot ${pt}.or.dot
    dot -Tpdf -o ${pt}.or.dot.pdf ${pt}.or.dot
    echo ""
    echo "----------------------------------------------------"
    echo "PRP->LN->LG optimized completed"
    echo "----------------------------------------------------"

#############################################################

    if [[ ${pt} == *_err* ]]; then
        echo "----------------------------------------------------"
        echo "Pass! This is a Compile Error Test, No Need to Generate Verilog Code "
        echo "----------------------------------------------------"
    fi
    #${LGSHELL} "lgraph.open name:${pt} path:lgdb2 |> inou.graphviz.from verbose:false"
#############################################################
    echo ""
    echo "----------------------------------------------------"
    echo "LG->LN starting..."
    echo "----------------------------------------------------"
    ${LGSHELL} "lgraph.open name:${pt} |> pass.lgraph_to_lnast |> inou.graphviz.from"
    if [ $? -eq 0 ]; then
      echo "Successfully converted LGraph to LNAST: ${pt}.prp"
    else
      echo "ERROR: LG -> LN pass failed: ${pt}.prp"
      exit 1
    fi
    ${LGSHELL} "lgraph.open name:${pt} |> pass.lgraph_to_lnast |> lnast.dump"
    dot -Tpdf -o ${pt}.lnast.dot.pdf ${pt}.lnast.dot
    echo ""
    echo "----------------------------------------------------"
    echo "LG->LN completed"
    echo "----------------------------------------------------"

    ${LGSHELL} "lgraph.open name:${pt} |> pass.lgraph_to_lnast |> inou.code_gen.prp"
    if [ $? -eq 0 ]; then
      echo "Successful code generation: ${pt}.prp"
    else
      echo "ERROR: code generation failed: ${pt}.cfg"
      exit 1
    fi


    rm -f ${pt}.cfg
    rm -f lnast.dot
    rm -f lnast.dot.gld
    rm -f lnast.nodes
    rm -f lnast.nodes.gld
done #end of for
