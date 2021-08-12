#!/bin/bash

pts_to_be_merged='io_gen io_gen2 io_gen3 test2'
pts_tuple_dbg='lhs_wire3 funcall_unnamed2
               firrtl_gcd counter_tup counter2'

pts_long_time='firrtl_gcd'

# pts_tbd='tup_out1 tup_out2'
pts_after_micro='hier_tuple4 tuple_reg3'


pts=' get_mask1 '
# scalar_tuple firrtl_tail3 hier_tuple bits_rhs logic2 
# hier_tuple_io hier_tuple3 tuple_if ssa_rhs out_ssa attr_set lhs_wire tuple_copy if1     
# lhs_wire2 tuple_copy2 tuple_empty_attr if2
# hier_tuple_nested_if3
# hier_tuple_nested_if6 hier_tuple_nested_if7 firrtl_tail
# tuple_nested1 tuple_nested2
# hier_tuple2 capricious_bits
# capricious_bits2 capricious_bits4 hier_tuple_nested_if 
# hier_tuple_nested_if4 
# '
#FIXME: for LL LN 
# firrtl_tail2 counter_1 firrtl_gcd_3bits  struct_flop tuple_reg tuple_reg2 nested_if counter_nested_if hier_tuple_nested_if5 reg_bits_set flatten_bundle partial 

#FIXME: for LL LN ... V gen fail!
# reg__q_pin  adder_stage  scalar_reg_out_pre_declare  vec_shift_register_param    

#FIXME: for LL LN ... LEC test fail!
# hier_tuple_nested_if2

# FIXME->Jose: these are the patterns require an extra cprop after bitwidth?
# pts='hier_tuple2 capricious_bits capricious_bits2 capricious_bits4'

# FIXME: extra flop left around!! (the test fails because this extra flop has no name and cgen creates incorrect verilog)
# pts='counter_mix'  
# pts='memory_1rd1wr'
# pts='masked_smem'
# pts='pp'
# pts='memory_1rd1wr'
# pts='pp2'
# pts='vector'
# pts='vector2'
# pts='hier_tuple_nested_if8'  # LNAST_TO failure
# pts='tuple_if2'

# Note: in this bash script, you MUST specify top module name AT FIRST POSITION
pts_hier1='top top_sum top'
pts_hier2='top top top_sum'


LGSHELL=./bazel-bin/main/lgshell
LGCHECK=./inou/yosys/lgcheck
PATTERN_PATH=./inou/pyrope/tests

if [ ! -f $LGSHELL ]; then
    if [ -f ./main/lgshell ]; then
        LGSHELL=./main/lgshell
        echo "lgshell is in $(pwd)"
    else
        echo "ERROR: could not find lgshell binary in $(pwd)";
    fi
fi


Pyrope_compile_LL_LN () {
  echo ""
  echo ""
  echo ""
  echo "===================================================="
  echo "Pyrope Full Compilation to check low level LN generation "
  echo " P->LN->LG(in lgdb2)->LN(low-level)-> code_gen prp -> LG(in lgdb) -> V (LEC)"
  echo "===================================================="


  for pt in $1
  do
    if [ ! -f ${PATTERN_PATH}/${pt}.prp ]; then
        echo "ERROR: could not find ${pt}.prp in ${PATTERN_PATH}"
        exit 1
    fi

    echo ""
    echo ""
    echo ""
    echo "----------------------------------------------------"
    echo "PRP -> HL LNAST -> LGraph"
    echo "----------------------------------------------------"
    #PRP->LN->LG
    ${LGSHELL} "inou.pyrope files:${PATTERN_PATH}/${pt}.prp |> pass.compiler gviz:true top:${pt} path:lgdb2 |> lgraph.dump"
    
    ret_val=$?
    if [ $ret_val -ne 0 ]; then
      echo "ERROR: could not compile with pattern: ${PATTERN_PATH}/${pt}.prp!"
      exit $ret_val
    fi
    
    echo ""
    echo ""
    echo ""
    echo "----------------------------------------------------"
    echo "LGraph -> LL LNAST -> pyrope_gen"
    echo "----------------------------------------------------"
    #LG->LN->LG
    ${LGSHELL} "lgraph.open name:${pt} path:lgdb2 |> inou.graphviz.from odir:gvizes" 
    dot -Tpdf -o gvizes/${pt}.dot.pdf gvizes/${pt}.dot
    ${LGSHELL} "lgraph.open name:${pt} path:lgdb2 |> pass.lnast_fromlg |> lnast.dump |> inou.code_gen.prp odir:tmp_prp" #This is just to see the LN generated


    ret_val=$?
    if [ $ret_val -ne 0 ]; then
      echo "ERROR: could not compile LG from lgdb2 with pattern: ${pt}.prp!"
      exit $ret_val
    fi
    
    if [ ! -f tmp_prp/${pt}.prp ]; then
        echo "ERROR: could not find ${pt}.prp in tmp_prp"
        exit 1
    fi
    echo ""
    echo ""
    echo ""
    echo "----------------------------------------------------"
    echo "pyrope_gen -> 2nd HL LNAST -> LGraph"
    echo "----------------------------------------------------"

    ${LGSHELL} "inou.pyrope files:tmp_prp/${pt}.prp |> pass.compiler gviz:true top:${pt} path:lgdb/ |> lgraph.dump"

    ret_val=$?
    if [ $ret_val -ne 0 ]; then
      echo "ERROR: could not compile LG to lgdb with pattern: tmp_prp/${pt}.prp!"
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
    ${LGSHELL} "lgraph.open name:${pt} path:lgdb/ |> inou.cgen.verilog"
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

Pyrope_compile_hier_LL_LN () {
  echo ""
  echo ""
  echo ""
  echo "===================================================="
  echo "Hierarchical Pyrope Full Compilation to check high level LN generation (code_gen)"
  echo "===================================================="

  declare pts_concat
  declare top_module
  declare tmp_pts_concat

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

    if [ -z "${tmp_pts_concat}" ]; then
      tmp_pts_concat="tmp_prp/${pt}.prp"
    else
      tmp_pts_concat="${tmp_pts_concat}, tmp_prp/${pt}.prp"
    fi
  done

  ${LGSHELL} "inou.pyrope files:${pts_concat} |> inou.code_gen.prp odir:tmp_prp"
  ret_val=$?
  if [ $ret_val -ne 0 ]; then
    echo "ERROR: could not compile with pattern: ${pts_concat}.prp!"
    exit $ret_val
  fi
  ${LGSHELL} "inou.pyrope files:${tmp_pts_concat} |> pass.compiler gviz:true top:${top_module}"
  #${LGSHELL} "inou.pyrope files:${pts_concat} |> pass.compiler top:${top_module}"
  ret_val=$?
  if [ $ret_val -ne 0 ]; then
    echo "ERROR: could not compile with pattern: ${tmp_pts_concat}.prp!"
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
    ${LGSHELL} "lgraph.open name:${pt} |> inou.cgen.verilog"
    if [ $? -eq 0 ] && [ -f ${pt}.v ]; then
        echo "Successfully generate Verilog: ${pt}.v"
        rm -f  yosys_script.*
    else
        echo "ERROR: Pyrope compiler failed: verilog generation, testcase: tmp_prp/${pt}.prp"
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

  ${LGCHECK} --top $top_module --implementation ${top_module}.v --reference ./inou/pyrope/tests/verilog_gld/${top_module}.gld.v

  if [ $? -eq 0 ]; then
      echo "Successfully pass logic equivilence check!"
  else
      echo "FAIL: ${top_module}.v !== ${top_module}.gld.v"
      exit 1
  fi
}

rm -r tmp_prp*
#rm -rf ./lgdb*
#Pyrope_compile_hier_LL_LN "$pts_hier1"
#rm -rf ./lgdb*
#Pyrope_compile_hier_LL_LN "$pts_hier2"
rm -rf ./lgdb*
Pyrope_compile_LL_LN "$pts"
 
rm -f *.v
rm -f ./*.dot
rm -f ./*.dot.pdf
rm -f ./lgcheck*
rm -f ./*.tcl
rm -f logger_*.log
rm -rf gvizes*
