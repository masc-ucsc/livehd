#!/bin/bash
rm -rf ./lgdb

pts='tuple_if tuple_if2 tuple_if3 nested_if if if2 if3_err nested_if_err ssa_rhs logic'
# pus='tuple_if2'
# pts='tuple_if2 tuple_if ssa_rhs ssa_nested_if ssa_if nested_if tuple simple_tuple function_call tuple '
# pts='ssa_rhs'
# pts='tuple'
# pts='trivial_bitwidth'
# pts='ssa_no_else_if'
# pts='function_call'

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
    echo ""
    echo ""
    echo ""
    echo "---------------------------------------------------"
    echo "CFG -> LNAST -> Graphviz Test"
    echo "---------------------------------------------------"
    echo "Pattern:${pt}.cfg"
    echo "Pattern:${pt}.cfg"
    echo "Pattern:${pt}.cfg"
    echo ""
    
    if [ ! -f inou/cfg/tests/${pt}.cfg ]; then
      echo "ERROR: could not find ${pt}.cfg in /inou/cfg/tests"
      exit !
    fi

    ln -s inou/cfg/tests/${pt}.cfg;

    ${LGSHELL} "inou.graphviz.fromlnast files:${pt}.cfg"

    if [ -f ${pt}.lnast.dot ]; then
      echo "Successfully create a lnast from ${pt}.cfg"
    else
      echo "ERROR: Pyrope compiler failed: LNAST generation, testcase: ${pt}.cfg"
      exit 1
    fi
    
    cat ${pt}.lnast.dot | sort -n > lnast.nodes
    cat ./inou/cfg/tests/dot_gld/${pt}.lnast.dot.gld | sort -n > lnast.nodes.gld

    diff lnast.nodes lnast.nodes.gld
    exit_code=$? 

    if [[ $exit_code == 0 ]]; then
      echo "Successfully match the golden LNAST, testcase: ${pt}.cfg"
    else 
      echo "ERROR: Pyrope compiler failed: generated LNAST doesn't match the golden, testcase: ${pt}.cfg"
      exit 1
    fi
    
    echo ""
    echo ""
    echo ""
    echo "===================================================="
    echo "Pyrope Full Compilation"  
    echo "===================================================="

    echo "----------------------------------------------------"
    echo "CFG -> LNAST -> LGraph"  
    echo "----------------------------------------------------"
    
    # ${LGSHELL} "inou.lnast_dfg.tolg files:${pt}.cfg"
    ${LGSHELL} "inou.cfg files:${pt}.cfg |> inou.lnast_dfg.tolg"
    if [ $? -eq 0 ]; then
      echo "Successfully create the inital LGraph with tuples: ${pt}.cfg"
    else
      echo "ERROR: Pyrope compiler failed: LNAST -> LGraph, testcase: ${pt}.cfg"
      exit 1

    fi


    ${LGSHELL} "lgraph.open name:${pt} |> inou.graphviz.from verbose:false"
    mv ${pt}.dot ${pt}.no_bits.tuple.reduced_or.dot


    echo ""
    echo ""
    echo ""
    echo "----------------------------------------------------"
    echo "Reduced_Or_Op Elimination(LGraph)"  
    echo "----------------------------------------------------"
    ${LGSHELL} "lgraph.open name:${pt} |> inou.lnast_dfg.reduced_or_elimination"
    if [ $? -eq 0 ]; then
      echo "Successfully eliminate all reduced_or_op: ${pt}.cfg"
    else
      echo "ERROR: Pyrope compiler failed: reduced_or_elimination, testcase: ${pt}.cfg"
      exit 1
    fi

    ${LGSHELL} "lgraph.open name:${pt} |> inou.graphviz.from verbose:false"
    mv ${pt}.dot ${pt}.no_bits.tuple.dot


    echo ""
    echo ""
    echo ""
    echo "----------------------------------------------------"
    echo "Tuple Chain Resolve(LGraph)"  
    echo "----------------------------------------------------"
    ${LGSHELL} "lgraph.open name:${pt} |> inou.lnast_dfg.resolve_tuples"
    if [ $? -eq 0 ]; then
      echo "Successfully resolve the tuple chain: ${pt}.cfg"
    else
      echo "ERROR: Pyrope compiler failed: resolve tuples, testcase: ${pt}.cfg"
      exit 1
    fi

    ${LGSHELL} "lgraph.open name:${pt} |> inou.graphviz.from verbose:false"
    mv ${pt}.dot ${pt}.no_bits.dot

    echo ""
    echo ""
    echo ""
    echo "----------------------------------------------------"
    echo "Bitwidth Optimization(LGraph)"  
    echo "----------------------------------------------------"

    ${LGSHELL} "lgraph.open name:${pt} |> pass.bitwidth"
    if [ $? -eq 0 ]; then
      echo "Successfully optimize design bitwidth: ${pt}.v"
    else
      echo "ERROR: Pyrope compiler failed: bitwidth optimization, testcase: ${pt}.cfg"
      exit 1
    fi

    ${LGSHELL} "lgraph.open name:${pt} |> inou.graphviz.from verbose:false"

    echo ""
    echo ""
    echo ""
    echo "----------------------------------------------------"
    echo "Dead Code Elimination(LGraph)"  
    echo "----------------------------------------------------"
    echo "Todo ..."

    # SUB='_err'

    if [[ ${pt} == *_err* ]]; then 
        echo "----------------------------------------------------"
        echo "Pass! This is a Compile Error Test, No Need to Generate Verilog Code "  
        echo "----------------------------------------------------"
    else
        echo ""
        echo ""
        echo ""
        echo "----------------------------------------------------"
        echo "LGraph -> Verilog"  
        echo "----------------------------------------------------"

        ${LGSHELL} "lgraph.open name:${pt} |> inou.yosys.fromlg"
        if [ $? -eq 0 ] && [ -f ${pt}.v ]; then
          echo "Successfully generate Verilog: ${pt}.v"
          rm -f  yosys_script.*
        else
          echo "ERROR: Pyrope compiler failed: verilog generation, testcase: ${pt}.cfg"
          exit 1
        fi


        echo ""
        echo ""
        echo ""
        echo "----------------------------------------------------"
        echo "Logic Equivalence Check"  
        echo "----------------------------------------------------"

        ${LGCHECK} --implementation=${pt}.v --reference=./inou/cfg/tests/verilog_gld/${pt}.gld.v

        if [ $? -eq 0 ]; then
          echo "Successfully pass logic equivilence check!"
        else
          echo "FAIL: "${pt}".v !== "${pt}".gld.v"
          exit 1
        fi
    fi



    rm -f ${pt}.v
    rm -f ${pt}.cfg
    rm -f lnast.dot.gld
    rm -f lnast.nodes
    rm -f lnast.nodes.gld
    # rm -f lnast.dot
    # rm -f *.dot
done #end of for



