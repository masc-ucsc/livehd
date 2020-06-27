#!/bin/bash
rm -rf ./lgdb

pts='SimpleBitOps Ops Test1 HwachaSequencer ICache' # RegisterSimple Flop Register
#HwachaSequencer -- printf, pad, stop

#SubModule BundleConnect -- submodules
#TrivialArith, Test3 -- pad op
#Test2 -- range, bit_sel op
#Test4 -- as_... ops in FIRRTL
#Test5 -- as_... ops in FIRRTL

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
    echo "===================================================="
    echo "Verify LoFIRRTL -> LNAST"
    echo "===================================================="


    echo "----------------------------------------------------"
    echo "LoFIRRTL -> LNAST-SSA Graphviz debug"
    echo "----------------------------------------------------"

    ${LGSHELL} "inou.firrtl.tolnast files:inou/firrtl/tests/proto/${pt}.lo.pb |> inou.lnast_dfg.dbg_lnast_ssa |> inou.graphviz.from"

    if [ -f ${pt}.lnast.dot ]; then
      echo "Successfully create a ssa lnast for debug: ${pt}.lo.pb"
    else
      echo "ERROR: LoFIRRTL -> LNAST -> LNAST-SSA failed... testcase: ${pt}.lo.pb"
      exit 1
    fi


    echo "----------------------------------------------------"
    echo "LoFIRRTL (Proto) -> LNAST -> LGraph"
    echo "----------------------------------------------------"

    ${LGSHELL} "inou.firrtl.tolnast files:inou/firrtl/tests/proto/${pt}.lo.pb |> inou.lnast_dfg.tolg"
    if [ $? -eq 0 ]; then
      echo "Successfully translated FIRRTL to LNAST to LGraph: ${pt}"
    else
      echo "ERROR: FIRRTL -> LNAST -> LGraph failed... testcase: ${pt}"
      exit 1
    fi
    ${LGSHELL} "lgraph.open name:${pt} |> inou.graphviz.from verbose:false"
    mv ${pt}.dot ${pt}.tuple.no_bits.or.dot

    echo ""
    echo ""
    echo ""
    echo "----------------------------------------------------"
    echo "Tuple Chain Resolve"
    echo "----------------------------------------------------"

    ${LGSHELL} "lgraph.open name:${pt} |> inou.lnast_dfg.resolve_tuples"
    if [ $? -eq 0 ]; then
      echo "Successfully resolve the tuple chain in new lg: ${pt}.lo.pb"
    else
      echo "ERROR: Pyrope compiler failed on new lg: resolve tuples, testcase: ${pt}.lo.pb"
      exit 1
    fi
    ${LGSHELL} "lgraph.open name:${pt} |> inou.graphviz.from verbose:false"
    mv ${pt}.dot ${pt}.no_bits.or.dot

    echo ""
    echo ""
    echo ""
    echo "----------------------------------------------------"
    echo "Bitwidth Optimization"
    echo "----------------------------------------------------"

    ${LGSHELL} "lgraph.open name:${pt} |> pass.bitwidth |> lgraph.dump"
    if [ $? -eq 0 ]; then
      echo "Successfully optimize design bitwidth on new lg: ${pt}.lo.pb"
    else
      echo "ERROR: Pyrope compiler failed on new lg: bitwidth optimization, testcase: ${pt}.lo.pb"
      exit 1
    fi
    ${LGSHELL} "lgraph.open name:${pt} |> inou.graphviz.from verbose:false"
    mv ${pt}.dot ${pt}.or.dot

    echo ""
    echo ""
    echo ""
    echo "----------------------------------------------------"
    echo "Reduced_Or_Op Elimination"
    echo "----------------------------------------------------"

    ${LGSHELL} "lgraph.open name:${pt} |> inou.lnast_dfg.assignment_or_elimination"
    if [ $? -eq 0 ]; then
      echo "Successfully eliminate all reduced_or_op in new lg: ${pt}.lo.pb"
    else
      echo "ERROR: Pyrope compiler failed on new lg: assignment_or_elimination, testcase: ${pt}.lo.pb"
      exit 1
    fi
    ${LGSHELL} "lgraph.open name:${pt} |> inou.graphviz.from verbose:false"
    mv ${pt}.dot ${pt}.newlg.dot

    echo ""
    echo ""
    echo ""
    echo "----------------------------------------------------"
    echo "Dead Code Elimination"
    echo "----------------------------------------------------"
    ${LGSHELL} "lgraph.open name:${pt} |> inou.lnast_dfg.dce"
    if [ $? -eq 0 ]; then
      echo "Successfully perform dead code elimination: ${pt}.lo.pb"
    else
      echo "ERROR: Pyrope compiler failed on new lg: dead code elimination, testcase: ${pt}.lo.pb"
      exit 1
    fi

    ${LGSHELL} "lgraph.open name:${pt} |> inou.graphviz.from verbose:false"
    mv ${pt}.dot ${pt}.newlg.dce.dot

    echo ""
    echo ""
    echo ""
    echo "----------------------------------------------------"
    echo "LGraph -> Verilog"
    echo "----------------------------------------------------"

    ${LGSHELL} "lgraph.open name:${pt} |> inou.yosys.fromlg"
    if [ $? -eq 0 ] && [ -f ${pt}.v ]; then
      echo "Successfully generate Verilog: ${pt}.v"
      exit 1
      rm -f  yosys_script.*
    else
      echo "ERROR: Yosys failed: verilog generation, testcase: ${pt}.lo.pb"
      exit 1
    fi

#    echo ""
#    echo ""
#    echo ""
#    echo "----------------------------------------------------"
#    echo "Logic Equivalence Check"
#    echo "----------------------------------------------------"
#
#    ${LGCHECK} --implementation=${pt}.v --reference=./inou/firrtl/tests/verilog_gld/${pt}.lo.v
#
#    if [ $? -eq 0 ]; then
#      echo "Successfully pass logic equivilence check!"
#    else
#      echo "FAIL: "${pt}".v !== "${pt}".lo.v (golden)"
#      exit 1
#    fi
#    ${LGSHELL} "lgraph.open name:${pt} |> inou.graphviz.from verbose:false"

done #end of for
