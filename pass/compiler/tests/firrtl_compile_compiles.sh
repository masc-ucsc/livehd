#!/bin/bash

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

PATTERN_PATH=./inou/firrtl/tests/proto
if [ -f "${PATTERN_PATH}/${file}.${FIRRTL_LEVEL}.pb" ]; then
  echo "Could not access test ${pts} at path ${PATTERN_PATH}"
  exit 1
fi

FIRRTL_LEVEL='ch'
pts='FMADecoder_1 Mul54 PipelinedMultiplier RecFNToIN PredRenameStage
RenameMapTable MulDiv IssueSlot_1 CSRFile RegisterFileSynthesizable ALUUnit
FPToInt ALU DecodeUnit '
# MulAddRecFNToRaw_preMul DivSqrtRecF64ToRaw_mulAddZ31 
# DivUnit 
firrtl_test() {
  echo ""
  echo ""
  echo ""
  echo "======================================================================"
  echo "                         ${FIRRTL_LEVEL}FIRRTL Full Compilation"
  echo "======================================================================"
  for pt in $pts
  do
    if [ ! -f ${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.pb ]; then
        echo "ERROR: could not find ${pt}.${FIRRTL_LEVEL}.pb in ${PATTERN_PATH}"
        exit 1
    fi
    rm -rf ./lgdb_${pt}
    ${LGSHELL} "inou.firrtl.tolnast files:inou/firrtl/tests/proto/${pt}.${FIRRTL_LEVEL}.pb |> lnast.dump " > ${pt}.lnast.raw.txt
    ${LGSHELL} "inou.firrtl.tolnast files:inou/firrtl/tests/proto/${pt}.${FIRRTL_LEVEL}.pb |> pass.lnast_tolg.dbg_lnast_ssa |> lnast.dump " > ${pt}.lnast.txt
    ${LGSHELL} "inou.firrtl.tolnast path:lgdb_${pt} files:${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.pb|> pass.compiler gviz:true top:${pt} firrtl:true path:lgdb_${pt} |> lgraph.save hier:true"
    ret_val=$?
    if [ $ret_val -ne 0 ]; then
      echo "ERROR: could not compile with pattern: ${pt}.${FIRRTL_LEVEL}.pb!"
      exit $ret_val
    fi

  # Verilog code generation
    echo ""
    echo ""
    echo ""
    echo "----------------------------------------------------"
    echo "LGraph -> Verilog"
    echo "----------------------------------------------------"

    rm -rf tmp_firrtl
    ${LGSHELL} "lgraph.open path:lgdb_${pt} name:${pt} hier:true |> inou.cgen.verilog odir:tmp_firrtl"
    # ${LGSHELL} "lgraph.open name:${pt} |> inou.cgen.verilog odir:tmp_firrtl"
    cat tmp_firrtl/*.v >tmp_firrtl/top_${pt}.v
    # ${LGSHELL} "lgraph.open name:${pt} |> inou.yosys.fromlg hier:true"
    ret_val=$?
    # ${LGSHELL} "lgraph.open name:${pt} |> inou.yosys.fromlg"
    if [ $ret_val -eq 0 ] && [ -f "tmp_firrtl/top_${pt}.v" ]; then
        echo "Successfully generate Verilog: tmp_firrtl/top_${pt}.v"
        echo "-----------------------------------------------------"
        echo "-----------------------------------------------------"
        echo "-----------------------------------------------------"
        echo "-----------------------------------------------------"
        rm -f  yosys_script.*
    else
        echo "ERROR: Firrtl compiler failed: verilog generation, testcase: ${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.pb"
        exit $ret_val
    fi
  done
}

firrtl_test "$pts"
