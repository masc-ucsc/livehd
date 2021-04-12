#!/bin/bash
rm -rf ./lgdb

pts_long_lec='GCD '

pts_todo_advanced='Risc FPU ICache MemoryController RWSmem Smem Rob ICache
HwachaSequencer RocketCore Ops Router'

pts_mem='Smem_simple Stack DynamicMemorySearch Memo'


if [ $# -eq 0 ]; then
  echo "Default regression set"
  # passed lofirrtl pattern pool
  pts='Life Cell_alone RegisterSimple Register Adder4 Mux4 LogShifter
  SingleEvenFilter RegXor AddNot VendingMachineSwitch Coverage VendingMachine
  VecShiftRegister Counter VecSearch ResetShiftRegister Parity
  EnableShiftRegister GCD_3bits Flop Accumulator LFSR16 BundleConnect SubModule
  Decrementer Test1 Test2 Test3 Test6 TrivialAdd NotAnd Trivial Tail TrivialArith
  Shifts PlusAnd MaxN ByteSelector Darken HiLoMultiplier SimpleALU Mul
  VecShiftRegisterParam VecShiftRegisterSimple '

  # pts_hifirrtl_todo='
  # passed hifirrtl pattern pool
  pts='EnableShiftRegister Flop MaxN PlusAnd Test2 
  Coverage Decrementer
  Xor6Thread2 XorSelfThread1 ByteSelector SimpleALU Mux4 Max2 ResetShiftRegister
  Parity RegisterSimple Register RegXor GCD_3bits Test3 TrivialAdd
  Accumulator AddNot HiLoMultiplier Darken Shifts NotAnd TrivialArith Tail Trivial
  LFSR16 VendingMachine VendingMachineSwitch'

  PATTERN_PATH=./inou/firrtl/tests/proto
  # FIRRTL_LEVEL='lo'
  FIRRTL_LEVEL='hi'

  # pts='VecShiftRegister'
  # pts='Cell_alone'
  # pts='SingleEvenFilter'
  # pts='SubModule'
  # pts='BundleConnect'
  # pts='LogShifter'
  # pts='Adder4'
  pts='Decrementer Counter '
  pts='Counter '
  # pts='Decrementer '

else
  file=$(basename $1)
  if [ "${file#*.}" == "hi.pb" ]; then
    echo "Using High Level FIRRTL"
    pts=$(basename $1 ".hi.pb")
    FIRRTL_LEVEL='hi'
  elif [ "${file#*.}" == "lo.pb" ]; then
    echo "Using Low Level FIRRTL"
    pts=$(basename $1 ".lo.pb")
    FIRRTL_LEVEL='lo'
  elif [ "${file#*.}" == "ch.pb" ]; then
    pts=$(basename $1 ".ch.pb")
    FIRRTL_LEVEL='ch'
    echo "Warning: Experimental Chirrtl extension"
  else
    echo "Illegal FIRRTL extension. Either ch.pb, hi.pb or lo.pb"
    exit 1
  fi

  PATTERN_PATH=$(dirname $1)
  if [ -f "${PATTERN_PATH}/${file}.${FIRRTL_LEVEL}.pb" ]; then
    echo "Could not access test ${pts} at path ${PATTERN_PATH}"
    exit 1
  fi
fi

pts_wait_verilog_large_mux_code_gen='Mul Test6 Test1'

# pts='Test1'
# pts='Life'
# pts='VecShiftRegisterParam'
# pts='VecShiftRegisterSimple '
# pts='VecSearch '
# pts='Flop'
# pts='VecShiftRegister '



LGSHELL=./bazel-bin/main/lgshell
LGCHECK=./inou/yosys/lgcheck
POST_IO_RENAME=./inou/firrtl/post_io_renaming.py

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
  echo "======================================================================"
  echo "                         HiFIRRTL Full Compilation"
  echo "======================================================================"
  for pt in $1
  do
    if [ ! -f ${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.pb ]; then
        echo "ERROR: could not find ${pt}.${FIRRTL_LEVEL}.pb in ${PATTERN_PATH}"
        exit 1
    fi

    ${LGSHELL} "inou.firrtl.tolnast files:${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.pb |> pass.compiler gviz:true top:${pt} firrtl:true"
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

    ${LGSHELL} "lgraph.open name:${pt} |> inou.cgen.verilog"
    # ${LGSHELL} "lgraph.open name:${pt} |> inou.yosys.fromlg hier:true"
    ret_val=$?
    # ${LGSHELL} "lgraph.open name:${pt} |> inou.yosys.fromlg"
    if [ $ret_val -eq 0 ] && [ -f ${pt}.v ]; then
        echo "Successfully generate Verilog: ${pt}.v"
        rm -f  yosys_script.*
    else
        echo "ERROR: Firrtl compiler failed: verilog generation, testcase: ${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.pb"
        exit $ret_val
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

    if [ "${FIRRTL_LEVEL}" == "hi" ] || [ "${FIRRTL_LEVEL}" == "ch" ]; then
        python3 ${POST_IO_RENAME} "${pt}.v"
    fi

    ${LGCHECK} --implementation=${pt}.v --reference=./inou/firrtl/tests/verilog_gld/${pt}.gld.v
    ret_val=$?
    if [ $ret_val -eq 0 ]; then
      echo "Successfully pass LEC!"
    else
        echo "FAIL: ${pt}.v !== ${pt}.gld.v"
        exit $ret_val
    fi
  done

  # rm -f *.v
  # rm -f *.dot
  # rm -f *.tcl
  # rm -f lgcheck*
  # rm -rf lgdb
}

firrtl_test "$pts"
