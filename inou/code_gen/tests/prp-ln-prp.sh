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
    echo "Pyrope -> LNAST "
    echo "----------------------------------------------------"

    ${LGSHELL} "inou.pyrope files:inou/${folder}/tests/${pt}.prp |> lnast.dump"
    if [ $? -eq 0 ]; then
      echo "Successfully converted prp->LN: ${pt}.cfg"
    else
      echo "ERROR: Pyrope compiler failed: PRP -> LNAST, testcase: ${pt}.cfg"
      exit 1
    fi

    ${LGSHELL} "inou.pyrope files:inou/${folder}/tests/${pt}.prp |> inou.graphviz.from verbose:false"
    if [ $? -eq 0 ]; then
      echo "Successfully converted LGraph to LNAST: ${pt}.prp"
    else
      echo "ERROR: LG -> LN pass failed: ${pt}.prp"
      exit 1
    fi
    dot -Tpdf -o ${pt}.lnast.dot.pdf ${pt}.lnast.dot

    ${LGSHELL} "inou.pyrope files:inou/${folder}/tests/${pt}.prp |> inou.code_gen.prp"
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
