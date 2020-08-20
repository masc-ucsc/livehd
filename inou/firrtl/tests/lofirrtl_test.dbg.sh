#!/bin/bash
rm -rf ./lgdb


pts='Trivial TrivialArith TrivialAdd NotAnd
     Test1 Test2
     BundleCombiner Flop MemoryController Tail
     RegisterSimple Register
     GCD Rob MemoryController ICache HwachaSequencer'
#pts='RegXor Decrementer GCD regex'

pts='GCD'
pts_hier='FinalVal2Test'
pts_hier2='FinalValTest'
pts_hier3='SubModule'
pts_hier4='BundleConnect'

pts_hier ='FPU'
pts_hier9='RocketCore'

#SimpleBitOps Ops -- parity and mod op not in lnast_dfg

#HwachaSequencer -- printf, pad, stop

#SubModule BundleConnect -- submodules
#Test3 -- fails because of DCE
#Test4 -- as_... ops in FIRRTL

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

lofirrtl_test() {
  for pt in $1
  do
    rm -f ${pt}.v
    rm -f ${pt}.dot
    rm -f lgdb/*


    echo "----------------------------------------------------"
    echo "LoFIRRTL-> RAW LNAST"
    echo "----------------------------------------------------"
  
    ${LGSHELL} "inou.firrtl.tolnast files:inou/firrtl/tests/proto/${pt}.lo.pb |> inou.graphviz.from"
  
    if [ -f ${pt}.lnast.dot ]; then
      echo "Successfully create raw lnast from inou/firrtl/tests/proto/${pt}.lo.pb"
    else
      echo "ERROR: Pyrope compiler failed: raw LNAST generation, testcase: ${pt}.lo.pb"
      exit 1
    fi

    mv ${pt}.lnast.dot ${pt}.lnast.raw.dot


    echo "----------------------------------------------------"
    echo "LoFIRRTL-> SSAed LNAST"
    echo "----------------------------------------------------"
  
    ${LGSHELL} "inou.firrtl.tolnast files:inou/firrtl/tests/proto/${pt}.lo.pb |> pass.lnast_dfg.dbg_lnast_ssa |> inou.graphviz.from"
  
    if [ -f ${pt}.lnast.dot ]; then
      echo "Successfully create ssaed lnast from inou/firrtl/tests/proto/${pt}.lo.pb"
    else
      echo "ERROR: Pyrope compiler failed: ssaed LNAST generation, testcase: ${pt}.lo.pb"
      exit 1
    fi


    echo ""
    echo "===================================================="
    echo "Verify LoFIRRTL -> LNAST"
    echo "===================================================="

    echo "----------------------------------------------------"
    echo "LoFIRRTL -> LNAST -> Optimized LGraph"
    echo "----------------------------------------------------"

    ${LGSHELL} "inou.firrtl.tolnast files:inou/firrtl/tests/proto/${pt}.lo.pb |> lnast.dump |> pass.lnast_dfg |> pass.cprop |> pass.bitwidth |> pass.cprop |> pass.bitwidth |> pass.cprop |> pass.bitwidth |> pass.bitwidth"
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
    echo "Generating Verilog: ${pt}"
    ${LGSHELL} "lgraph.match |> inou.yosys.fromlg"
    if [ $? -eq 0 ] && [ -f ${pt}.v ]; then
      echo "Successfully generate Verilog: ${pt}.v"
      rm -f  yosys_script.*
    else
      echo "ERROR: Yosys failed: verilog generation, testcase: ${pt}.lo.pb"
      exit 1
    fi

    if [[ $3 == "hier" ]]; then
      top_module=$1
      echo $top_module

      for sub in $2
      do
        $(cat ${sub}.v >> ${top_module}.v)
      done
    fi

    echo ""
    echo ""
    echo ""
    echo "----------------------------------------------------"
    echo "Logic Equivalence Check"
    echo "----------------------------------------------------"

    ${LGCHECK} --implementation=${pt}.v --reference=./inou/firrtl/tests/verilog_gld/${pt}.v

    if [ $? -eq 0 ]; then
      echo "Successfully pass logic equivilence check!"
    else
      echo "FAIL: "${pt}".v !== "${pt}".v (golden)"
      exit 1
    fi

    # rm -f *.v
    # rm -f *.dot
    # rm -f lgdb/*
    # rm -f yosys.*
  done
}

lofirrtl_test "$pts"
# If testing a module with submodules in it, put the name of the
# top module as the first argument then list all the submodules
# in the entire design as the second argument, and "hier" as the
# third agument.
lofirrtl_test "$pts_hier"  "Sum" "hier"
lofirrtl_test "$pts_hier2" "Sum" "hier"
#lofirrtl_test "$pts_hier3" "SubModuleSubMod" "hier"
#lofirrtl_test "$pts_hier4" "BundleConnectSubMod" "hier"

#lofirrtl_test "$pts_hier9" "IBuf CSRFile BreakpointUnit ALU MulDiv RVCExpander" "hier"
