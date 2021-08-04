#!/bin/bash

rm -rf ./lgdb

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
  echo "                         ${FIRRTL_LEVEL}FIRRTL Full Compilation"
  echo "======================================================================"
  for pt in $1
  do
    if [ ! -f ${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.pb ]; then
        echo "ERROR: could not find ${pt}.${FIRRTL_LEVEL}.pb in ${PATTERN_PATH}"
        exit 1
    fi
    ${LGSHELL} "inou.firrtl.tolnast files:inou/firrtl/tests/proto/${pt}.${FIRRTL_LEVEL}.pb |> pass.lnast_tolg.dbg_lnast_ssa |> lnast.dump " > ${pt}.lnast.txt
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
    # ${LGSHELL} "lgraph.open name:${pt} |> inou.cgen.verilog odir:tmp_firrtl"
    cat tmp_firrtl/*.v >tmp_firrtl/top_${pt}.v
    # ${LGSHELL} "lgraph.open name:${pt} |> inou.yosys.fromlg hier:true"
    ret_val=$?
    # ${LGSHELL} "lgraph.open name:${pt} |> inou.yosys.fromlg"
    if [ $ret_val -eq 0 ] && [ -f "tmp_firrtl/top_${pt}.v" ]; then
        echo "Successfully generate Verilog: tmp_firrtl/top_${pt}.v"
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

    cp -f "./inou/firrtl/tests/verilog_gld/${pt}.gld.v" "tmp_firrtl/${pt}.gld.v"
    if [ "${FIRRTL_LEVEL}" == "hi" ] || [ "${FIRRTL_LEVEL}" == "ch" ]; then
        echo "Running python script to match IO names between LiveHD and FIRRTL compiler"
        python3 ${POST_IO_RENAME} "tmp_firrtl/top_${pt}.v"  "tmp_firrtl/${pt}.gld.v"
    fi

    ${LGCHECK} --top ${pt} --implementation "tmp_firrtl/top_${pt}.v" --reference "tmp_firrtl/${pt}.gld.v"
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
