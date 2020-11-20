#!/bin/bash
rm -rf ./lgdb

pts_todo='RegXor Decrementer GCD regex MemoryController Rob ICache HwachaSequencer'
pts_handle_1st='TrivialArith '
pts='GCD Trivial TrivialAdd NotAnd Test1 Test2 BundleCombiner 
     Flop Tail RegisterSimple Register '

pts_hier='FinalVal2Test'
pts_hier2='FinalValTest'
pts_hier3='SubModule'
pts_hier4='BundleConnect'

pts_hier='FPU'
pts_hier9='RocketCore'

#SimpleBitOps Ops -- parity and mod op not in lnast_tolg

#HwachaSequencer -- printf, pad, stop

#SubModule BundleConnect -- submodules
#Test3 -- fails because of DCE
#Test4 -- as_... ops in FIRRTL

LGSHELL=./bazel-bin/main/lgshell
LGCHECK=./inou/yosys/lgcheck
PATTERN_PATH=./inou/firrtl/tests/proto

if [ ! -f $LGSHELL ]; then
    if [ -f ./main/lgshell ]; then
        LGSHELL=./main/lgshell
        echo "lgshell is in $(pwd)"
    else
        echo "ERROR: could not find lgshell binary in $(pwd)";
    fi
fi

lofirrtl_test() {
  echo ""
  echo ""
  echo ""
  echo "===================================================="
  echo "LoFIRRTL Full Compilation"
  echo "===================================================="


  for pt in $1
  do
    if [ ! -f ${PATTERN_PATH}/${pt}.lo.pb ]; then
        echo "ERROR: could not find ${pt}.lo.pb in ${PATTERN_PATH}"
        exit 1
    fi

    ${LGSHELL} "inou.firrtl.tolnast files:${PATTERN_PATH}/${pt}.lo.pb |> pass.compiler gviz:true"
    ret_val=$?
    if [ $ret_val -ne 0 ]; then
      echo "ERROR: could not compile with pattern: ${pt}.lo.pb!"
      exit $ret_val
    fi
  done #end of for


  # Verilog code generation
  for pt in $1
  do
    echo ""
    echo ""
    echo ""
    echo "----------------------------------------------------"
    echo "LGraph -> Verilog"
    echo "----------------------------------------------------"

    ${LGSHELL} "lgraph.open name:${pt} |> inou.yosys.fromlg hier:true"
    if [ $? -eq 0 ] && [ -f ${pt}.v ]; then
        echo "Successfully generate Verilog: ${pt}.v"
        rm -f  yosys_script.*
    else
        echo "ERROR: Pyrope compiler failed: verilog generation, testcase: ${PATTERN_PATH}/${pt}.lo.pb"
        exit 1
    fi
  done


  # Logic Equivalence Check
  for pt in $1
  do
    echo ""
    echo ""
    echo ""
    echo "----------------------------------------------------"
    echo "Logic Equivalence Check"
    echo "----------------------------------------------------"

    ${LGCHECK} --implementation=${pt}.v --reference=./inou/firrtl/tests/verilog_gld/${pt}.gld.v

    if [ $? -eq 0 ]; then
      echo "Successfully pass LEC!"
    else
        echo "FAIL: "${pt}".v !== "${pt}".gld.v"
        exit 1
    fi
  done

    # rm -f *.v
    # rm -f *.dot
    # rm -f lgdb/*
    # rm -f yosys.*
}

lofirrtl_test "$pts"
# If testing a module with submodules in it, put the name of the
# top module as the first argument then list all the submodules
# in the entire design as the second argument, and "hier" as the
# third agument.
# lofirrtl_test "$pts_hier"  "Sum" "hier"
# lofirrtl_test "$pts_hier2" "Sum" "hier"
# lofirrtl_test "$pts_hier3" "SubModuleSubMod" "hier"
# lofirrtl_test "$pts_hier4" "BundleConnectSubMod" "hier"

# lofirrtl_test "$pts_hier9" "IBuf CSRFile BreakpointUnit ALU MulDiv RVCExpander" "hier"
