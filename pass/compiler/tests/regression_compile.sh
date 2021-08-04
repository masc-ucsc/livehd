#!/bin/bash

#----------------------------------------- Firrtl start --------------------------------

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

  # passed hifirrtl pattern pool
  PATTERN_PATH=./inou/firrtl/tests/proto
  # FIRRTL_LEVEL='lo'
  FIRRTL_LEVEL='hi'

  pts='VecShiftRegisterSimple VecShiftRegisterParam VecShiftRegister Cell_alone
  Accumulator Coverage LFSR16 TrivialAdd VendingMachineSwitch
  VendingMachine Trivial Tail TrivialArith NotAnd Shifts Darken HiLoMultiplier
  AddNot GCD_3bits Test3 Register RegisterSimple Parity ResetShiftRegister
  SimpleALU ByteSelector Test2 MaxN Max2 Flop EnableShiftRegister LogShifter
  Decrementer Counter RegXor Mux4 Adder4 BundleConnect SubModule
  SingleEvenFilter Xor6Thread2 XorSelfThread1 PlusAnd '

  # issue: bits TupAdd doesn't converted to attribute set
  # pts='XorSelfThread1'

  # pts='PlusAnd'        # issue: first element of Vector is not ended with _0
  # pts='Test1'          # issue: run-time vector index
  # pts='Adder4'

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

    rm -rf tmp_firrtl
    ${LGSHELL} "lgraph.open name:${pt} hier:true |> inou.cgen.verilog odir:tmp_firrtl"
    cat tmp_firrtl/*.v >${pt}.v
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

    ${LGCHECK} --implementation ${pt}.v --reference ./inou/firrtl/tests/verilog_gld/${pt}.gld.v
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



#----------------------------------------- Pyrope start --------------------------------

#!/bin/bash

pts='reg_bits_set bits_rhs reg__q_pin scalar_tuple hier_tuple_io hier_tuple3
hier_tuple2 tuple_if ssa_rhs out_ssa attr_set if2 hier_tuple lhs_wire
tuple_copy if hier_tuple_nested_if2 lhs_wire2 tuple_copy2 counter lhs_wire
adder_stage capricious_bits4 capricious_bits logic capricious_bits2
scalar_reg_out_pre_declare firrtl_tail2 hier_tuple_nested_if
hier_tuple_nested_if3 hier_tuple_nested_if4 hier_tuple_nested_if5
hier_tuple_nested_if6 hier_tuple_nested_if7 firrtl_tail firrtl_gcd_3bits
nested_if firrtl_tail3 counter_nested_if tuple_empty_attr tuple_reg tuple_reg2
struct_flop tuple_nested1 tuple_nested2 get_mask1 counter_mix vec_shift_register_param'



# pts="tuple_reg tuple_reg2" zero init due to cprop seems to break it

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
    #${LGSHELL} "inou.pyrope files:${PATTERN_PATH}/${pt}.prp |> pass.compiler top:${pt}"
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

    # ${LGSHELL} "lgraph.open name:${pt} |> inou.yosys.fromlg hier:true"
    ${LGSHELL} "lgraph.open name:${pt} |> inou.cgen.verilog"
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

      ${LGCHECK} --implementation ${pt}.v --reference ${PATTERN_PATH}/verilog_gld/${pt}.gld.v

      if [ $? -eq 0 ]; then
        echo "Successfully pass LEC!"
      else
          echo "FAIL: ${pt}.v !== ${pt}.gld.v"
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
  #${LGSHELL} "inou.pyrope files:${pts_concat} |> pass.compiler top:${top_module}"
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

    #${LGSHELL} "lgraph.open name:${pt} |> inou.yosys.fromlg hier:true"
    ${LGSHELL} "lgraph.open name:${pt} |> inou.cgen.verilog "
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

  ${LGCHECK} --top $top_module --implementation ${top_module}.v --reference ./inou/pyrope/tests/compiler/verilog_gld/${top_module}.gld.v

  if [ $? -eq 0 ]; then
      echo "Successfully pass logic equivilence check!"
  else
      echo "FAIL: ${top_module}.v !== ${top_module}.gld.v"
      exit 1
  fi
}

rm -rf ./lgdb
Pyrope_compile "$pts"
rm -rf ./lgdb
Pyrope_compile_hier "$pts_hier1"
rm -rf ./lgdb
Pyrope_compile_hier "$pts_hier2"


