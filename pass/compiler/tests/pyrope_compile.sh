#!/bin/bash
rm -rf ./lgdb

pts_to_be_merged='io_gen io_gen2 io_gen3 test2'
pts_tuple_dbg='firrtl_tail2 
               hier_tuple_io  
               lhs_wire3 tuple funcall_unnamed2 
               firrtl_tail3 firrtl_gcd counter_tup counter2'

pts_1st_priority='reg__q_pin'

pts='if2 hier_tuple2 nested_if adder_stage if bits_rhs capricious_bits2 capricious_bits4 
     hier_tuple logic reg_bits_set tuple_copy hier_tuple3 lhs_wire lhs_wire2 scalar_tuple
     firrtl_tail attr_set capricious_bits out_ssa ssa_rhs counter counter_nested_if tuple_if'

pts='firrtl_tail2'
pts='reg__q_pin'


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

Pyrope_compile () {
  echo ""
  echo ""
  echo ""
  echo "===================================================="
  echo "Pyrope Full Compilation"
  echo "===================================================="


  for pt in $1
  do
    if [ ! -f inou/pyrope/tests/compiler/${pt}.prp ]; then
        echo "ERROR: could not find ${pt}.prp in /inou/pyrope/tests/compiler"
        exit 1
    fi

    ${LGSHELL} "inou.pyrope files:inou/pyrope/tests/compiler/${pt}.prp |> pass.compiler gviz:true"
    ret_val=$?
    if [ $ret_val -ne 0 ]; then
      echo "ERROR: could not compile with pattern: ${pt}.prp!"
      exit $ret_val
    fi
  done #end of for


  # Verilog code generation
  for pt in $1
  do
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

          ${LGSHELL} "lgraph.open name:${pt} |> inou.yosys.fromlg hier:true"
          if [ $? -eq 0 ] && [ -f ${pt}.v ]; then
              echo "Successfully generate Verilog: ${pt}.v"
              rm -f  yosys_script.*
          else
              echo "ERROR: Pyrope compiler failed: verilog generation, testcase: inou/pyrope/tests/compiler/${pt}.prp"
              exit 1
          fi
      fi
  done


  # Logic Equivalence Check
  if [[ $2 == "hier" ]]; then
      #get the last pattern of pts_hier
      top_module=$(echo $1 | awk '{print $NF}')
      echo $top_module

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
  else
      for pt in $1
      do
          echo ""
          echo ""
          echo ""
          echo "----------------------------------------------------"
          echo "Logic Equivalence Check"
          echo "----------------------------------------------------"

          ${LGCHECK} --implementation=${pt}.v --reference=./inou/pyrope/tests/compiler/verilog_gld/${pt}.gld.v

          if [ $? -eq 0 ]; then
            echo "Successfully pass LEC!"
          else
              echo "FAIL: "${pt}".v !== "${pt}".gld.v"
              exit 1
          fi
      done
  fi
}




Pyrope_compile "$pts"
# Pyrope_compile "$pts_hier"  "hier"
# Pyrope_compile "$pts_hier2" "hier"
# Pyrope_compile "$pts_hier4" "hier"
# Pyrope_compile "$pts_hier5" "hier"
# Pyrope_compile "$pts_hier6" "hier"

# rm -f *.dot
# rm -f *.v
# rm -f lgcheck*
