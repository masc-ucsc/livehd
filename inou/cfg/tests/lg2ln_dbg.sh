#!/bin/bash
rm -rf ./lgdb

#make sure to call Pyrope_compile() in the end of script
pts='test'
# pts_hier='sum funcall'
# pts_hier2='sum funcall4'   


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
    if [ ! -f inou/cfg/tests/${pt}.prp ]; then
      echo "ERROR: could not find ${pt}.prp in /inou/cfg/tests"
      exit !
    fi
  
    # ln -s inou/cfg/tests/${pt}.prp;
  
    echo "----------------------------------------------------"
    echo "Pyrope -> LNAST-SSA Graphviz debug"
    echo "----------------------------------------------------"
  
    ${LGSHELL} "inou.pyrope files:inou/cfg/tests/${pt}.prp |> inou.lnast_dfg.dbg_lnast_ssa |> inou.graphviz.from"
  
    if [ -f ${pt}.lnast.dot ]; then
      echo "Successfully create a lnast from inou/cfg/tests/${pt}.prp"
    else
      echo "ERROR: Pyrope compiler failed: LNAST generation, testcase: ${pt}.prp"
      exit 1
    fi
  
    if true ; then
      echo "----------------------------------------------------"
      echo "Pyrope -> LNAST -> LGraph"
      echo "----------------------------------------------------"
  
      # ${LGSHELL} "inou.lnast_dfg.tolg files:${pt}.cfg"
      ${LGSHELL} "inou.pyrope files:inou/cfg/tests/${pt}.prp |> inou.lnast_dfg.tolg"
      if [ $? -eq 0 ]; then
        echo "Successfully create the inital LGraph: inou/cfg/tests/${pt}.prp"
      else
        echo "ERROR: Pyrope compiler failed: LNAST -> LGraph, testcase: inou/cfg/tests/${pt}.prp"
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
      #${LGSHELL} "lgraph.open name:${pt} |> inou.lnast_dfg.resolve_tuples"
      ${LGSHELL} "lgraph.open name:${pt} |> pass.cprop"
      if [ $? -eq 0 ]; then
        echo "Successfully resolve the tuple chain: inou/cfg/tests/${pt}.prp"
      else
        echo "ERROR: Pyrope compiler failed: resolve tuples, testcase: inou/cfg/tests/${pt}.prp"
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
        echo "Successfully optimize design bitwidth: inou/cfg/tests/${pt}.prp"
      else
        echo "ERROR: Pyrope compiler failed: bitwidth optimization, testcase: inou/cfg/tests/${pt}.prp"
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
        echo "ERROR: Pyrope compiler failed: verilog generation, testcase: inou/cfg/tests/${pt}.prp"
        exit 1
      fi
    fi
  done



  if [[ $2 == "hier" ]]; then
    echo ""
  else
    for pt in $1
    do
      echo ""
      echo ""
      echo ""
      echo "----------------------------------------------------"
      echo "LGraph -> LNAST"
      echo "----------------------------------------------------"
    
      ${LGSHELL} "lgraph.open name:${pt} |> pass.lgraph_to_lnast"
    
      if [ $? -eq 0 ]; then
        echo "Successfully generate lnast from lgraph! :${pt}.prp"
      else
        echo "cannot generate lnast from lgraph :${pt}.prp"
        exit 1
      fi

      echo ""
      echo ""
      echo ""
      echo "----------------------------------------------------"
      echo "LGraph -> LNAST -> DOT"
      echo "----------------------------------------------------"
    
      ${LGSHELL} "lgraph.open name:${pt} |> pass.lgraph_to_lnast |> inou.graphviz.from"
    

      if [ $? -eq 0 ]; then
        echo "Successfully generate lnast from lgraph! :${pt}.prp"
      else
        echo "cannot generate lnast from lgraph :${pt}.prp"
        exit 1
      fi
      mv ${pt}.lnast.dot ${pt}.lnast.dot.itr2


      echo ""
      echo ""
      echo ""
      echo "----------------------------------------------------"
      echo "LGraph -> LNAST -> SSA -> DOT"
      echo "----------------------------------------------------"
    
      ${LGSHELL} "lgraph.open name:${pt} |> pass.lgraph_to_lnast |> inou.lnast_dfg.dbg_lnast_ssa |> lnast.dump"
    

      if [ $? -eq 0 ]; then
        echo "Successfully generate lnast from lgraph! :${pt}.prp"
      else
        echo "cannot generate lnast from lgraph :${pt}.prp"
        exit 1
      fi
      mv ${pt}.lnast.dot ${pt}.lnast.dot.itr2.ssa

      echo ""
      echo ""
      echo ""
      echo "----------------------------------------------------"
      echo "LGraph -> LNAST -> LGraph"
      echo "----------------------------------------------------"
    
      ${LGSHELL} "lgraph.open name:${pt} |> pass.lgraph_to_lnast |> inou.lnast_dfg.tolg" 
    

      if [ $? -eq 0 ]; then
        echo "Successfully generate lgraph from lnast! :${pt}.prp"
      else
        echo "cannot generate lgraph from lnast :${pt}.prp"
        exit 1
      fi

      mv ${pt}.dot ${pt}.raw.dot.itr2
      echo ""
      echo ""
      echo ""
      echo "----------------------------------------------------"
      echo "Copy-Propagation And Tuple Chain Resolve"
      echo "----------------------------------------------------"
      #${LGSHELL} "lgraph.open name:${pt} |> inou.lnast_dfg.resolve_tuples"
      ${LGSHELL} "lgraph.open name:${pt} |> pass.cprop"
      if [ $? -eq 0 ]; then
        echo "Successfully resolve the tuple chain: inou/cfg/tests/${pt}.prp"
      else
        echo "ERROR: Pyrope compiler failed: resolve tuples, testcase: inou/cfg/tests/${pt}.prp"
        exit 1
      fi
  
      ${LGSHELL} "lgraph.open name:${pt} |> inou.graphviz.from verbose:false"
      mv ${pt}.dot ${pt}.no_bits.dot.itr2
  
  
      echo ""
      echo ""
      echo ""
      echo "----------------------------------------------------"
      echo "Bitwidth Optimization(LGraph)"
      echo "----------------------------------------------------"
  
      ${LGSHELL} "lgraph.open name:${pt} |> pass.bitwidth |> pass.cprop |> pass.bitwidth |> pass.cprop |> pass.bitwidth |> pass.bitwidth"
      if [ $? -eq 0 ]; then
        echo "Successfully optimize design bitwidth: inou/cfg/tests/${pt}.prp"
      else
        echo "ERROR: Pyrope compiler failed: bitwidth optimization, testcase: inou/cfg/tests/${pt}.prp"
        exit 1
      fi
  
      ${LGSHELL} "lgraph.open name:${pt} |> inou.graphviz.from verbose:false"
      mv ${pt}.dot ${pt}.dot.itr2


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
        echo "ERROR: Pyrope compiler failed: verilog generation, testcase: inou/cfg/tests/${pt}.prp"
        exit 1
      fi


    done
  fi
}

Pyrope_compile "$pts" 
# Pyrope_compile "$pts_hier" "hier"
# Pyrope_compile "$pts_hier2" "hier"


# rm -f *.v
# rm -f lnast.dot.gld
# rm -f lnast.nodes
# rm -f lnast.nodes.gld
# rm -f *.dot
