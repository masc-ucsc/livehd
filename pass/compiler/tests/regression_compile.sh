
#----------------------------------------- Firrtl start --------------------------------

#!/bin/bash
rm -rf ./lgdb
# FIRRTL_LEVEL='lo'
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

# pts_hifirrtl_todo='Test6 ByteSelector ResetShiftRegister Counter Life Cell_alone Adder4 Mux4 LogShifter SingleEvenFilter
# VecShiftRegister BundleConnect SubModule PlusAnd MaxN VecShiftRegisterParam
# VecShiftRegisterSimple VecSearch VendingMachineSwitch VendingMachine'

pts='EnableShiftRegister Cell_alone MaxN PlusAnd Test2 SingleEvenFilter
Coverage Counter Decrementer SubModule BundleConnect LogShifter Adder4
Xor6Thread2 XorSelfThread1 ByteSelector SimpleALU Mux4 Max2 ResetShiftRegister
Parity RegisterSimple Register RegXor AddNot GCD_3bits Flop Test3 TrivialAdd
NotAnd Trivial Tail TrivialArith Shifts Darken HiLoMultiplier Accumulator
LFSR16 VendingMachine VendingMachineSwitch'  


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

  rm -f *.v
  rm -f *.dot
  rm -f lgcheck*
  rm -rf lgdb
  rm -f *.tcl
}

firrtl_test "$pts"



#----------------------------------------- Pyrope start --------------------------------

#!/bin/bash

pts_to_be_merged='io_gen io_gen2 io_gen3 test2'
pts_tuple_dbg='lhs_wire3 funcall_unnamed2
               firrtl_gcd counter_tup counter2'

pts_long_time='firrtl_gcd'

pts='reg_bits_set bits_rhs reg__q_pin scalar_tuple hier_tuple_io hier_tuple3
hier_tuple2 tuple_if ssa_rhs out_ssa attr_set if2 hier_tuple lhs_wire
tuple_copy if firrtl_tail hier_tuple_nested_if2 lhs_wire2 tuple_copy2
counter_nested_if counter lhs_wire adder_stage capricious_bits4 capricious_bits
firrtl_gcd_3bits nested_if firrtl_tail3 logic capricious_bits2
scalar_reg_out_pre_declare firrtl_tail2 tuple_reg tuple_reg2'


# pts='firrtl_tail reg_bits_set  reg_bits_set firrtl_tail2 firrtl_tail3 firrtl_gcd_3bits  tuple_copy2 '

# Note: in this bash script, you MUST specify top module name AT FIRST POSITION
pts_hier1='top sum top'
pts_hier2='top top sum'


LGSHELL=./bazel-bin/main/lgshell
LGCHECK=./inou/yosys/lgcheck
PATTERN_PATH=./inou/pyrope/tests/compiler

if [ ! -f $LGSHELL ]; then
    if [ -f ./main/lgshell ]; then
        LGSHELL=./main/lgshell
        echo "lgshell is in $(pwd)"
    else
        echo "ERROR: could not find lgshell binary in $(pwd)";
    fi
fi


Pyrope_compile () {
  echo ""
  echo ""
  echo ""
  echo "===================================================="
  echo "Pyrope Full Compilation"
  echo "===================================================="


  for pt in $1
  do
    if [ ! -f ${PATTERN_PATH}/${pt}.prp ]; then
        echo "ERROR: could not find ${pt}.prp in ${PATTERN_PATH}"
        exit 1
    fi

    ${LGSHELL} "inou.pyrope files:${PATTERN_PATH}/${pt}.prp |> pass.compiler gviz:true top:${pt}"
    ret_val=$?
    if [ $ret_val -ne 0 ]; then
      echo "ERROR: could not compile with pattern: ${pt}.prp!"
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
        echo "ERROR: Pyrope compiler failed: verilog generation, testcase: ${PATTERN_PATH}/${pt}.prp"
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

      ${LGCHECK} --implementation=${pt}.v --reference=${PATTERN_PATH}/verilog_gld/${pt}.gld.v

      if [ $? -eq 0 ]; then
        echo "Successfully pass LEC!"
      else
          echo "FAIL: "${pt}".v !== "${pt}".gld.v"
          exit 1
      fi
  done
}


Pyrope_compile_hier () {
  echo ""
  echo ""
  echo ""
  echo "===================================================="
  echo "Hierarchical Pyrope Full Compilation"
  echo "===================================================="

  declare pts_concat
  declare top_module

  for pt in $1
  do
    if [ ! -f ${PATTERN_PATH}/${pt}.prp ]; then
        echo "ERROR: could not find ${pt}.prp in ${PATTERN_PATH}"
        exit 1
    fi

    # the first item in pts_hier is just specifying the top_module name
    if [ -z "${top_module}" ]; then
      top_module=${pt}
      continue
    fi

    # check if pts_concat is empty or not and perform pattern concatenation, patterns have to be comma seperated
    if [ -z "${pts_concat}" ]; then
      pts_concat="${PATTERN_PATH}/${pt}.prp"
    else
      pts_concat="${pts_concat}, ${PATTERN_PATH}/${pt}.prp"
    fi
  done


  ${LGSHELL} "inou.pyrope files:${pts_concat} |> pass.compiler gviz:true top:${top_module}"
  ret_val=$?
  if [ $ret_val -ne 0 ]; then
    echo "ERROR: could not compile with pattern: ${pts_concat}.prp!"
    exit $ret_val
  fi


  # Verilog code generation
  for pt in $1
  do
    echo ""
    echo ""
    echo ""
    echo "----------------------------------------------------"
    echo "LGraph -> Verilog"
    echo "----------------------------------------------------"

    # ${LGSHELL} "lgraph.open name:${pt} |> inou.yosys.fromlg hier:true"
    ${LGSHELL} "lgraph.open name:${pt} |> inou.yosys.fromlg"
    if [ $? -eq 0 ] && [ -f ${pt}.v ]; then
        echo "Successfully generate Verilog: ${pt}.v"
        rm -f  yosys_script.*
    else
        echo "ERROR: Pyrope compiler failed: verilog generation, testcase: ${PATTERN_PATH}/${pt}.prp"
        exit 1
    fi
  done

  #concatenate every submodule under top_module.v
  for pt in $1
  do
    if [[ pt != $top_module ]]; then
        $(cat ${pt}.v >> ${top_module}.v)
    fi
  done


  echo ""
  echo ""
  echo ""
  echo "----------------------------------------------------"
  echo "Logic Equivalence Check: Hierarchical Design"
  echo "----------------------------------------------------"

  ${LGCHECK} --top=$top_module --implementation=${top_module}.v --reference=./inou/pyrope/tests/compiler/verilog_gld/${top_module}.gld.v

  if [ $? -eq 0 ]; then
      echo "Successfully pass logic equivilence check!"
  else
      echo "FAIL: "${top_module}".v !== "${top_module}".gld.v"
      exit 1
  fi
}


rm -rf ./lgdb
Pyrope_compile_hier "$pts_hier1"
rm -rf ./lgdb
Pyrope_compile_hier "$pts_hier2"
rm -rf ./lgdb
Pyrope_compile "$pts"


rm -f *.dot
rm -f *.v
rm -f lgcheck*







