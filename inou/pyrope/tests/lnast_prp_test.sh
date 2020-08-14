#!/bin/bash
rm -rf ./lgdb

pts_to_do='lhs_wire3 tuple'
pts='reg_bits_set tuple_copy logic
     hier_tuple hier_tuple2 hier_tuple3 
     lhs_wire lhs_wire2 scalar_tuple attr_set
     firrtl_tail3 firrtl_tail2 firrtl_tail 
     adder_stage nested_if tuple_if reg__q_pin 
     capricious_bits2 capricious_bits4 capricious_bits 
     out_ssa if2 if ssa_rhs bits_rhs counter counter_nested_if
     '

#make sure to call Pyrope_compile() in the end of script
# pts='reg__q_pin'
pts_hier='sum funcall'
pts_hier2='sum2 funcall2'   
pts_hier4='funcall4'   


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
  echo "Pyrope Full Compilation (C++ Parser)"
  echo "===================================================="
  
  
  for pt in $1
  do
    if [ ! -f inou/pyrope/tests/compiler/${pt}.prp ]; then
      echo "ERROR: could not find ${pt}.prp in /inou/pyrope/tests/compiler"
      exit 2
    fi
  
    # ln -s inou/pyrope/tests/compiler/${pt}.prp;
  
    echo "----------------------------------------------------"
    echo "Pyrope -> LNAST-SSA Graphviz debug"
    echo "----------------------------------------------------"
  
    ${LGSHELL} "inou.pyrope files:inou/pyrope/tests/compiler/${pt}.prp |> pass.lnast_dfg.dbg_lnast_ssa |> inou.graphviz.from"
  
    if [ -f ${pt}.lnast.dot ]; then
      echo "Successfully create a lnast from inou/pyrope/tests/compiler/${pt}.prp"
    else
      echo "ERROR: Pyrope compiler failed: LNAST generation, testcase: ${pt}.prp"
      exit 1
    fi
  
    if true ; then
      echo "----------------------------------------------------"
      echo "Pyrope -> LNAST -> LGraph"
      echo "----------------------------------------------------"
  
      # ${LGSHELL} "pass.lnast_dfg files:${pt}.cfg"
      ${LGSHELL} "inou.pyrope files:inou/pyrope/tests/compiler/${pt}.prp |> pass.lnast_dfg"
      if [ $? -eq 0 ]; then
        echo "Successfully create the inital LGraph: inou/pyrope/tests/compiler/${pt}.prp"
      else
        echo "ERROR: Pyrope compiler failed: LNAST -> LGraph, testcase: inou/pyrope/tests/compiler/${pt}.prp"
        exit 1
  
      fi
  
      ${LGSHELL} "lgraph.open name:${pt} |> inou.graphviz.from verbose:false"
      mv ${pt}.dot ${pt}.raw.dot
  
      echo ""
      echo ""
      echo ""
      echo "----------------------------------------------------"
      echo "Copy-Propagation And Tuple Chain Resolve"
      echo "----------------------------------------------------"
      #${LGSHELL} "lgraph.open name:${pt} |> pass.lnast_dfg.resolve_tuples"
      ${LGSHELL} "lgraph.open name:${pt} |> pass.cprop"
      if [ $? -eq 0 ]; then
        echo "Successfully resolve the tuple chain: inou/pyrope/tests/compiler/${pt}.prp"
      else
        echo "ERROR: Pyrope compiler failed: resolve tuples, testcase: inou/pyrope/tests/compiler/${pt}.prp"
        exit 1
      fi
  
      ${LGSHELL} "lgraph.open name:${pt} |> inou.graphviz.from verbose:false"
      mv ${pt}.dot ${pt}.no_bits.dot
  
  
      echo ""
      echo ""
      echo ""
      echo "----------------------------------------------------"
      echo "Bitwidth Optimization(LGraph)"
      echo "----------------------------------------------------"
  
      ${LGSHELL} "lgraph.open name:${pt} |> pass.bitwidth |> pass.cprop |> pass.bitwidth |> pass.cprop |> pass.bitwidth |> pass.bitwidth"
      if [ $? -eq 0 ]; then
        echo "Successfully optimize design bitwidth: inou/pyrope/tests/compiler/${pt}.prp"
      else
        echo "ERROR: Pyrope compiler failed: bitwidth optimization, testcase: inou/pyrope/tests/compiler/${pt}.prp"
        exit 1
      fi
  
      ${LGSHELL} "lgraph.open name:${pt} |> inou.graphviz.from verbose:false"
    fi
  done #end of for
  
  
  
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
  
      ${LGSHELL} "lgraph.open name:${pt} |> inou.yosys.fromlg"
      if [ $? -eq 0 ] && [ -f ${pt}.v ]; then
        echo "Successfully generate Verilog: ${pt}.v"
        rm -f  yosys_script.*
      else
        echo "ERROR: Pyrope compiler failed: verilog generation, testcase: inou/pyrope/tests/compiler/${pt}.prp"
        exit 1
      fi
    fi
  done



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
        echo "Successfully pass logic equivilence check!"
      else
        echo "FAIL: "${pt}".v !== "${pt}".gld.v"
        exit 1
      fi
    done
  fi
}

Pyrope_compile "$pts" 
Pyrope_compile "$pts_hier"  "hier"
Pyrope_compile "$pts_hier2" "hier"
# Pyrope_compile "$pts_hier4" "hier"


rm -f *.v
rm -f lnast.dot.gld
rm -f lnast.nodes
rm -f lnast.nodes.gld
rm -f *.dot
