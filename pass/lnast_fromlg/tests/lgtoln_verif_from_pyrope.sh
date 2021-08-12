#!/bin/bash
rm -rf ./lgdb
rm -rf ./lgdb2

pts='tuple_copy logic
     hier_tuple hier_tuple2 hier_tuple3
     lhs_wire lhs_wire2 attr_set
     firrtl_tail
     reg__q_pin adder_stage
     nested_if tuple_if
     capricious_bits2 capricious_bits4 capricious_bits
     out_ssa if2 if ssa_rhs bits_rhs'

pts_hier='sum funcall'
pts_hier2='sum2 funcall2'

#TO ADD, BUT BUGS:
#  Hits mmap_lib assertion failure in pass.bitwidth
#     - scalar_tuple
#  Problems with registers (attr specified into ln during lg->ln don't all work yet in ln->lg)
#     - reg_bits_set
#     - firrtl_tail3
#     - firrtl_tail2 (also has problems with temp vars in ln->lg)
#     - adder_stage
#     - reg__q_pin
#     - counter
#     - counter_nested_if

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

lgtoln_verif() {
  for pt in $1
  do
    if [ -f ${pt}.v ]; then rm ${pt}.v; fi
    echo ""
    echo ""
    echo ""
    echo "===================================================="
    echo "Compilation to get stable Lgraph"
    echo "===================================================="

    echo "----------------------------------------------------"
    echo "Pyrope -> LNAST -> Lgraph"
    echo "----------------------------------------------------"

    ${LGSHELL} "inou.pyrope files:inou/pyrope/tests/compiler/${pt}.prp |> pass.lnast_tolg"
    if [ $? -eq 0 ]; then
      echo "Successfully created optimized Lgraph: ${pt}"
    else
      echo "ERROR: Pyrope compiler failed on Pyrope -> LNAST -> Lgraph, testcase: ${pt}"
      exit 1
    fi

    echo ""
    echo "----------------------------------------------------"
    echo "Lgraph optimization"
    echo "----------------------------------------------------"

    ${LGSHELL} "lgraph.open name:${pt} |> pass.cprop |> pass.bitwidth |> pass.cprop |> pass.bitwidth |> pass.cprop |> pass.bitwidth |> pass.bitwidth"
    if [ $? -eq 0 ]; then
      echo "Successfully optimize design bitwidth: ${pt}.v"
    else
      echo "ERROR: Pyrope compiler failed: bitwidth optimization, testcase: ${pt}"
      exit 1
    fi
    ${LGSHELL} "lgraph.open name:${pt} |> inou.graphviz.from verbose:false"
    mv ${pt}.dot ${pt}.oldlg.dot

#############################################################

    echo ""
    echo "===================================================="
    echo "LG-LNAST interface verification"
    echo "===================================================="

    echo "----------------------------------------------------"
    echo "Lgraph (golden) -> LNAST -> Lgraph (new)"
    echo "----------------------------------------------------"

    ${LGSHELL} "lgraph.open name:${pt} |> pass.lnast_fromlg |> lnast.dump |> pass.lnast_tolg path:lgdb2"
    if [ $? -eq 0 ]; then
      echo "Successfully create the new LG: ${pt}"
    else
      echo "ERROR: Tester failed: LG -> LNAST -> Lgraph, testcase: ${pt}"
      exit 1
    fi

    echo ""
    echo "----------------------------------------------------"
    echo "Lgraph Optimization (newlg)"
    echo "----------------------------------------------------"

    ${LGSHELL} "lgraph.open name:${pt} path:lgdb2 |> inou.graphviz.from |> pass.cprop |> pass.bitwidth |> pass.cprop |> pass.bitwidth |> pass.cprop |> pass.bitwidth |> pass.bitwidth"
    if [ $? -eq 0 ]; then
      echo "Successfully optimized newlg: ${pt}.v"
    else
      echo "ERROR: Failed optimizations on newlg, testcase: ${pt}"
      exit 1
    fi
    ${LGSHELL} "lgraph.open name:${pt} path:lgdb2 |> inou.graphviz.from verbose:false"
    mv ${pt}.dot ${pt}.newlg.dot

    echo ""
    echo "----------------------------------------------------"
    echo "Lgraph -> Verilog"
    echo "----------------------------------------------------"

    ${LGSHELL} "lgraph.open name:${pt} path:lgdb2 |> inou.yosys.fromlg"
    if [ $? -eq 0 ] && [ -f ${pt}.v ]; then
      echo "Successfully generate Verilog: ${pt}.v"
      rm -f  yosys_script.*
    else
      echo "ERROR: Verilog generation failed, testcase: ${pt}"
      exit 1
    fi

    # If not hierarchical test, then just do lgcheck after each file.
    if [[ $2 != "hier" ]]; then
      echo ""
      echo "----------------------------------------------------"
      echo "Logic Equivalence Check"
      echo "----------------------------------------------------"

      ${LGCHECK} --implementation ${pt}.v --reference ./inou/pyrope/tests/compiler/verilog_gld/${pt}.gld.v

      if [ $? -eq 0 ]; then
        echo "Successfully pass logic equivilence check!"
      else
        echo "FAIL: ${pt}.v !== ${pt}.gld.v"
        exit 1
      fi

      rm -f ./*.v
      rm -f ./*.dot
    fi
  done

  # If doing hierarchical, then we have to do all files and then lgcheck after.
  if [[ $2 == "hier" ]]; then
    top_module=$(echo $1 | awk '{print $NF}')
    echo $top_module

    for pt in $1
    do
      if [[ pt != $top_module ]]; then
        $(cat ${pt}.v >> ${top_module}.v)
      fi
    done

    echo ""
    echo "----------------------------------------------------"
    echo "Logic Equivalence Check"
    echo "----------------------------------------------------"

    ${LGCHECK} --implementation ${top_module}.v --reference ./inou/pyrope/tests/compiler/verilog_gld/${top_module}.gld.v

    if [ $? -eq 0 ]; then
      echo "Successfully pass logic equivilence check!"
    else
      echo "FAIL: ${top_module}.v !== ${top_module}.gld.v"
      exit 1
    fi

    rm -f ./*.v
    rm -f ./*.dot
  fi

  rm -rf ./lgdb
  rm -rf ./lgdb2
}

#lgtoln_verif "$pts"
lgtoln_verif "$pts_hier" "hier"
#lgtoln_verif "$pts_hier2" "hier"
