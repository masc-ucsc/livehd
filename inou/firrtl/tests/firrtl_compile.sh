#!/bin/bash
rm -rf ./lgdb
FIRRTL_LEVEL='lo'

pts_need_chisel_source_code='NotAnd'
pts_todo='MemoryController Rob ICache HwachaSequencer'
pts_long_lec='GCD'
pts_handle_1st='regex Test3 coverage'

pts='Decrementer RegXor TrivialAdd Test1 Test2 NotAnd
     BundleCombiner Flop Tail RegisterSimple Register TrivialArith GCD_3bits'

pts_hier='FinalVal2Test'
pts_hier2='FinalValTest'
pts_hier3='SubModule'
pts_hier4='BundleConnect'

pts_hier='FPU'
pts_hier9='RocketCore'

pts='Trivial AddNot'

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

firrtl_test() {
  echo ""
  echo ""
  echo ""
  echo "===================================================="
  echo "LoFIRRTL Full Compilation"
  echo "===================================================="


  for pt in $1
  do
    if [ ! -f ${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.pb ]; then
        echo "ERROR: could not find ${pt}.${FIRRTL_LEVEL}.pb in ${PATTERN_PATH}"
        exit 1
    fi

    ${LGSHELL} "inou.firrtl.tolnast files:${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.pb |> pass.compiler gviz:true top:${pt}"
    ret_val=$?
    if [ $ret_val -ne 0 ]; then
      echo "ERROR: could not compile with pattern: ${pt}.${FIRRTL_LEVEL}.pb!"
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
        echo "ERROR: Pyrope compiler failed: verilog generation, testcase: ${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.pb"
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

firrtl_test "$pts"
# If testing a module with submodules in it, put the name of the
# top module as the first argument then list all the submodules
# in the entire design as the second argument, and "hier" as the
# third agument.
# firrtl_test "$pts_hier"  "Sum" "hier"
# firrtl_test "$pts_hier2" "Sum" "hier"
# firrtl_test "$pts_hier3" "SubModuleSubMod" "hier"
# firrtl_test "$pts_hier4" "BundleConnectSubMod" "hier"

# firrtl_test "$pts_hier9" "IBuf CSRFile BreakpointUnit ALU MulDiv RVCExpander" "hier"
