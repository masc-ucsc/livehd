#!/bin/bash
rm -rf ./lgdb
FIRRTL_LEVEL='lo'
FIRRTL_LEVEL='hi'

pts_long_lec='GCD '

pts_todo_advanced='Risc FPU ICache MemoryController RWSmem Smem Rob ICache
HwachaSequencer RocketCore Ops Router'

pts_mem='Smem_simple Stack DynamicMemorySearch Memo'

# passed lofirrtl pattern pool
pts='Life Cell_alone RegisterSimple Register Adder4 Mux4 LogShifter
SingleEvenFilter RegXor AddNot VendingMachineSwitch Coverage VendingMachine
VecShiftRegister Counter VecSearch ResetShiftRegister Parity
EnableShiftRegister GCD_3bits Flop Accumulator LFSR16 BundleConnect SubModule
Decrementer Test1 Test2 Test3 Test6 TrivialAdd NotAnd Trivial Tail TrivialArith
Shifts PlusAnd MaxN ByteSelector Darken HiLoMultiplier SimpleALU Mul
VecShiftRegisterParam VecShiftRegisterSimple ' 

pts_hifirrtl_todo='Test6 LFSR16 Accumulator ByteSelector ResetShiftRegister Counter Life Cell_alone Adder4 Mux4 LogShifter SingleEvenFilter
VecShiftRegister BundleConnect SubModule PlusAnd MaxN VecShiftRegisterParam
VecShiftRegisterSimple VecSearch VendingMachineSwitch VendingMachine'

pts='MaxN RegisterSimple Register RegXor AddNot EnableShiftRegister GCD_3bits Flop
Decrementer Test2 Test3 TrivialAdd NotAnd Trivial Tail TrivialArith Shifts
Darken HiLoMultiplier Max2 Coverage 
' 

# pts='Mul'
# pts='SimpleALU'
# pts='Test1'
# pts='Test6'
# pts='LFSR16'
# pts='Accumulator'
# pts='ByteSelector'
# pts='ResetShiftRegister'
# pts='Counter'
# pts='Adder4'
# pts='Mux4'
# pts='LogShifter'
# pts='SingleEvenFilter'
# pts='Parity'


LGSHELL=./bazel-bin/main/lgshell
LGCHECK=./inou/yosys/lgcheck
POST_IO_RENAME=./inou/firrtl/post_io_renaming.py
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
  echo "======================================================================"
  echo "                         LoFIRRTL Full Compilation"
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

    ${LGSHELL} "lgraph.open name:${pt} |> inou.yosys.fromlg hier:true"
    # ${LGSHELL} "lgraph.open name:${pt} |> inou.yosys.fromlg"
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
    
    if [ "${FIRRTL_LEVEL}" == "hi" ]; then
        python ${POST_IO_RENAME} "${pt}.v"
    fi

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
  # rm -f lgcheck*
  # rm -rf lgdb
}

firrtl_test "$pts"
