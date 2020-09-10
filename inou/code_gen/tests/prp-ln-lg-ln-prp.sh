#!/bin/bash
rm -rf ./lgdb
rm -rf ./lgdb2
rm -rf ./*.dot*
rm -rf ./prp_ln_lg_ln_prp_dir

pts='long_gcd' #lnast_utest' #'funcall4' # tuple_if'
folder='pyrope'

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
    echo "----------------------------------------------------"
    echo "Pyrope -> LNAST -> LGraph"
    echo "----------------------------------------------------"

    #${LGSHELL} "inou.pyrope files:inou/${folder}/tests/compiler/${pt}.prp |> pass.lnast_tolg"
    ${LGSHELL} "inou.pyrope files:inou/${folder}/tests/${pt}.prp |> pass.lnast_tolg"
    #${LGSHELL} "inou.pyrope files:inou/code_gen/tests/${pt}.prp |> pass.lnast_tolg"
    #${LGSHELL} "inou.pyrope files:${pt}.prp |> pass.lnast_tolg |> lgraph.dump"
    if [ $? -eq 0 ]; then
      echo "Successfully create the inital LGraph with tuples: ${pt}.cfg"
    else
      echo "ERROR: Pyrope compiler failed: LNAST -> LGraph, testcase: ${pt}.cfg"
      exit 1
    fi

    ${LGSHELL} "lgraph.open name:${pt} |> inou.graphviz.from"
    mv ${pt}.dot ${pt}.raw.dot
    dot -Tpdf -o ${pt}.raw.dot.pdf ${pt}.raw.dot

    echo ""
    echo ""
    echo ""
    echo "----------------------------------------------------"
    echo "cprop and bitwidth"
    echo "----------------------------------------------------"

    ${LGSHELL} "lgraph.open name:${pt} |> pass.cprop |> pass.bitwidth |> pass.cprop |> pass.bitwidth |> pass.cprop |> lgraph.dump"
    if [ $? -eq 0 ]; then
      echo "Successfully optimize design bitwidth: ${pt}.v"
    else
      echo "ERROR: Pyrope compiler failed: bitwidth optimization, testcase: ${pt}.cfg"
      exit 1
    fi

    ${LGSHELL} "lgraph.open name:${pt} |> inou.graphviz.from"
    mv ${pt}.dot ${pt}.no_bits.dot
    dot -Tpdf -o ${pt}.no_bits.dot.pdf ${pt}.no_bits.dot
    echo ""
    echo "----------------------------------------------------"
    echo "PRP->LN->LG optimized completed"
    echo "----------------------------------------------------"

    if [[ ${pt} == *_err* ]]; then
      echo "----------------------------------------------------"
      echo "Pass! This is a Compile Error Test, No Need to Generate Verilog Code "
      echo "----------------------------------------------------"
    fi
    echo "----------------------------------------------------"
    echo "LG->LN starting..."
    echo "----------------------------------------------------"
    ${LGSHELL} "lgraph.open name:${pt} |> pass.lnast_fromlg |> lnast.dump |> inou.graphviz.from |> inou.code_gen.prp odir:prp_ln_lg_ln_prp_dir"
    if [ $? -eq 0 ]; then
      echo "Successfully converted LGraph to LNAST: ${pt}.prp and code_gen prp"
    else
      echo "ERROR: LG -> LN pass failed: ${pt}.prp"
      exit 1
    fi
    dot -Tpdf -o ${pt}.lnast.dot.pdf ${pt}.lnast.dot
    echo ""
    echo "----------------------------------------------------"
    echo "LG->LN completed"
    echo "----------------------------------------------------"

    rm -f ${pt}.cfg
    rm -f lnast.dot
    rm -f lnast.dot.gld
    rm -f lnast.nodes
    rm -f lnast.nodes.gld
done #end of for
