#!/bin/bash
rm -rf ./lgdb

pts='Test1 FinalVal2Test FinalValTest NotAnd Trivial RegTrivial Test1 RegTrivial RegisterSimple Flop Register GCD RocketCore ICache' #Flop Register
pts='BundleCombiner'

#SimpleBitOps Ops -- parity and mod op not in lnast_dfg

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
    if [ -f ${pt}.v ]; then rm ${pt}.v; fi
    if [ -f ${pt}.dot ]; then rm ${pt}.dot; fi
    if [ -d lgdb ]; then rm lgdb/*; fi

    echo ""
    echo ""
    echo ""
    echo "===================================================="
    echo "Verify LoFIRRTL -> LNAST"
    echo "===================================================="

    echo "----------------------------------------------------"
    echo "LoFIRRTL -> LNAST -> Optimized LGraph"
    echo "----------------------------------------------------"

    ${LGSHELL} "inou.firrtl.tolnast files:inou/firrtl/tests/proto/${pt}.lo.pb |> pass.lnast_dfg |> pass.cprop |> pass.bitwidth |> pass.cprop |> pass.bitwidth |> pass.cprop |> pass.bitwidth |> pass.bitwidth"
    if [ $? -eq 0 ]; then
      echo "Successfully translated FIRRTL to LNAST to LGraph: ${pt}.lo.pb"
    else
      echo "ERROR: FIRRTL -> LNAST -> LGraph failed... testcase: ${pt}.lo.pb"
      exit 1
    fi
    ${LGSHELL} "lgraph.match |> inou.graphviz.from"

    echo ""
    echo "----------------------------------------------------"
    echo "Optimized LGraph -> Verilog"
    echo "----------------------------------------------------"
    ${LGSHELL} "lgraph.match |> inou.yosys.fromlg"
    if [ $? -eq 0 ] && [ -f ${pt}.v ]; then
      echo "Successfully generate Verilog: ${pt}.v"
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
#    ${LGCHECK} --implementation=${pt}.v --reference=./inou/firrtl/tests/verilog_gld/${pt}.v
#
#    if [ $? -eq 0 ]; then
#      echo "Successfully pass logic equivilence check!"
#    else
#      echo "FAIL: "${pt}".v !== "${pt}".v (golden)"
#      exit 1
#    fi

done #end of for

rm ${pt}.v
rm ${pt}.dot
rm lgdb/*
rm yosys*
