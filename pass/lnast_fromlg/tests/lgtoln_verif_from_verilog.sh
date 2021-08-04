#!/bin/bash
rm -rf ./lgdb
rm -rf ./lgdb2

pts='trivial3 logic_bitwise_op_gld common_sub
     operators mux mux2 trivial
     trivial_and assigns compare trivial1
     loop_in_lg loop_in_lg2'

#pts='simple_add'

#TO ADD LIST, but have bugs:
#  Sign isn't working yet:
#     - add
#  0b0u1bit << 2 is treated as a 1 bit number (causes bw problems):
#     - pick
#  Node not yet handled in some other pass:
#     - consts (shift_left not yet handled in pass.bw)
#     - satsmall, satlarge (mult not supported in pass.bw)
#     - arith (mod op not yet suppoted in pass.lnast_tolg)
#  Problems with registers:
#     - simple_flop
#     - cse_basic
#     - shift
#  pass.lnast_tolg requires temp vars to be used immediately after being set:
#     - long_gcd
#  Submodules failing
#     - submodule
#     - trivial2
#  There's an error in out_connected_pins()... returns 0 pins for a node that has an output
#     - compare2

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
    rm -rf ./lgdb*
    if [ -f ${pt}.v ]; then rm ${pt}.v; fi

    echo "----------------------------------------------------"
    echo "Verilog -> Lgraph"
    echo "----------------------------------------------------"
    ${LGSHELL} "inou.yosys.tolg files:inou/yosys/tests/${pt}.v top:${pt} |> pass.cprop |> pass.bitwidth |> pass.cprop |> pass.bitwidth |> pass.cprop |> inou.graphviz.from"
    if [ $? -eq 0 ]; then
      echo "Successfully created the inital Lgraph using Yosys: ${pt}.v"
    else
      echo "ERROR: Verilog -> Lgraph failed... testcase: ${pt}.v"
      exit 1
    fi
    mv ${pt}.dot ${pt}.origlg.dot
    echo "----------------------------------------------------"
    echo "Lgraph -> LNAST"
    echo "----------------------------------------------------"
    ${LGSHELL} "lgraph.match |> pass.lnast_fromlg |> lnast.dump"
    if [ $? -eq 0 ]; then
      echo "Successfully created the inital Lgraph using Yosys: ${pt}.v"
    else
      echo "ERROR: Lgraph -> LNAST failed... testcase: ${pt}.v"
      exit 1
    fi
#    echo "----------------------------------------------------"
#    echo "Lgraph -> LNAST -> code_gen prp"
#    echo "----------------------------------------------------"
#    ${LGSHELL} "lgraph.match |> pass.lnast_fromlg |> lnast.dump |> inou.code_gen.prp"
#    if [ $? -eq 0 ]; then
#      echo "Successfully created the inital Lgraph using Yosys: ${pt}.v"
#    else
#      echo "ERROR: LNAST -> code_gen prp  failed... testcase: ${pt}.v"
#      exit 1
#    fi
    echo "----------------------------------------------------"
    echo "Lgraph -> LNAST -> lgraph2"
    echo "----------------------------------------------------"
    ${LGSHELL} "lgraph.match |> pass.lnast_fromlg |> pass.lnast_tolg path:lgdb2"
    if [ $? -eq 0 ]; then
      echo "Successfully created the inital Lgraph using Yosys: ${pt}.v"
    else
      echo "ERROR: Lgraph -> LNAST -> Lgraph2 failed... testcase: ${pt}.v"
      exit 1
    fi

    echo "----------------------------------------------------"
    echo "Lgraph Optimization"
    echo "----------------------------------------------------"
    ${LGSHELL} "lgraph.match path:lgdb2 |> pass.cprop |> pass.bitwidth |> pass.cprop |> pass.bitwidth |> pass.cprop |> pass.bitwidth |> pass.bitwidth"
    if [ $? -eq 0 ]; then
      echo "Successfully optimize design on new lg: ${pt}.v"
    else
      echo "ERROR: Failed to optimize design on new lg, testcase: ${pt}.v"
      exit 1
    fi
    ${LGSHELL} "lgraph.match path:lgdb2 |> inou.graphviz.from verbose:false"
    mv ${pt}.dot ${pt}.newlg.dot

    echo ""
    echo "----------------------------------------------------"
    echo "Lgraph -> Verilog"
    echo "----------------------------------------------------"

    ${LGSHELL} "lgraph.match path:lgdb2 |> inou.yosys.fromlg"
    if [ $? -eq 0 ] && [ -f ${pt}.v ]; then
      echo "Successfully generate Verilog: ${pt}.v"
      rm -f  yosys_script.*
    else
      echo "ERROR: Yosys failed: verilog generation, testcase: ${pt}.v"
      exit 1
    fi

    echo ""
    echo "----------------------------------------------------"
    echo "Logic Equivalence Check"
    echo "----------------------------------------------------"
    ${LGCHECK} --implementation ${pt}.v --reference ./inou/yosys/tests/${pt}.v
    if [ $? -eq 0 ]; then
      echo "Successfully pass logic equivilence check!"
    else
      echo "FAIL: ${pt}.v !== ${pt}.gld.v"
      exit 1
    fi
    ${LGSHELL} "lgraph.match path:lgdb2 |> inou.graphviz.from verbose:false"

done #end of for
