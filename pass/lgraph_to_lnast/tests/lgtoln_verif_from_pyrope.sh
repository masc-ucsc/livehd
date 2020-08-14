#!/bin/bash
rm -rf ./lgdb
rm -rf ./lgdb2

pts='tuple_copy logic
     hier_tuple hier_tuple2 hier_tuple3
     lhs_wire lhs_wire2 scalar_tuple attr_set
     firrtl_tail
     nested_if tuple_if
     capricious_bits2 capricious_bits4 capricious_bits
     out_ssa if2 if ssa_rhs bits_rhs'

#TO ADD, BUT BUGS:
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

for pt in $pts
do
    if [ -f ${pt}.v ]; then rm ${pt}.v; fi
    echo ""
    echo ""
    echo ""
    echo "===================================================="
    echo "Compilation to get stable LGraph"
    echo "===================================================="

    echo "----------------------------------------------------"
    echo "Pyrope -> LNAST -> LGraph"
    echo "----------------------------------------------------"

    ${LGSHELL} "inou.pyrope files:inou/pyrope/tests/compiler/${pt}.prp |> pass.lnast_dfg"
    if [ $? -eq 0 ]; then
      echo "Successfully created optimized LGraph: ${pt}"
    else
      echo "ERROR: Pyrope compiler failed on Pyrope -> LNAST -> LGraph, testcase: ${pt}"
      exit 1
    fi

    echo ""
    echo "----------------------------------------------------"
    echo "LGraph optimization"
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
    echo "LGraph (golden) -> LNAST -> LGraph (new)"
    echo "----------------------------------------------------"

    ${LGSHELL} "lgraph.open name:${pt} |> pass.lgraph_to_lnast |> lnast.dump |> pass.lnast_dfg path:lgdb2"
    if [ $? -eq 0 ]; then
      echo "Successfully create the new LG: ${pt}"
    else
      echo "ERROR: Tester failed: LG -> LNAST -> LGraph, testcase: ${pt}"
      exit 1
    fi

    echo ""
    echo "----------------------------------------------------"
    echo "LGraph Optimization (newlg)"
    echo "----------------------------------------------------"

    ${LGSHELL} "lgraph.open name:${pt} path:lgdb2 |> pass.cprop |> pass.bitwidth |> pass.cprop |> pass.bitwidth |> pass.cprop |> pass.bitwidth |> pass.bitwidth"
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
    echo "LGraph -> Verilog"
    echo "----------------------------------------------------"

    ${LGSHELL} "lgraph.open name:${pt} path:lgdb2 |> inou.yosys.fromlg"
    if [ $? -eq 0 ] && [ -f ${pt}.v ]; then
      echo "Successfully generate Verilog: ${pt}.v"
      rm -f  yosys_script.*
    else
      echo "ERROR: Verilog generation failed, testcase: ${pt}"
      exit 1
    fi

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

    rm -f ${pt}.v
    rm -f ${pt}.oldlg.dot
    rm -f ${pt}.newlg.dot
done #end of for

rm -rf ./lgdb
rm -rf ./lgdb2
