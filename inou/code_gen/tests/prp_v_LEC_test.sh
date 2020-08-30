#!/bin/bash        
rm -rf ./lgdb      
rm -rf ./lgdb2
rm -rf ./prp_v_LEC_test_dir
rm -f ./*dot*
rm -f *.v

pts='logic'

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

#referred lnast_prp_test.sh

echo "=============================================================="
echo "Flow:                                                         "
echo "  Prp_in -> LN -> LG -> LN -> Prp_out                         "
echo "                   |               |                          "
echo "                   V               V                          "
echo "                  Ver1            LN -> LG -> Ver2            "
echo "  Perform LEC b/w Ver1 and Ver2                                         "
echo "=============================================================="


#for pt in $1
for pt in $pts
do
  #checking whether the file exists or not
  if [ ! -f inou/pyrope/tests/compiler/${pt}.prp ]; then
    echo "ERROR: could not find ${pt}.prp in /inou/pyrope/tests/compiler"
    exit 2
  fi

  echo "--------------------------------------------------------------"
  echo "PRP -> LNAST -> LGraph                                        "
  echo "--------------------------------------------------------------"
  ${LGSHELL} "inou.pyrope files:inou/pyrope/tests/compiler/${pt}.prp |> pass.lnast_tolg"
  if [ $? -eq 0 ]; then
    echo "Successfully create the inital LGraph: inou/pyrope/tests/compiler/${pt}.prp"
  else
    echo "ERROR: Pyrope compiler failed: LNAST -> LGraph, testcase: inou/pyrope/tests/compiler/${pt}.prp"
    exit 1
  fi
  
  ${LGSHELL} "lgraph.open name:${pt} |> inou.graphviz.from"
  mv ${pt}.dot ${pt}.raw.dot
  dot -Tpdf -o ${pt}.raw.dot.pdf ${pt}.raw.dot

  echo "--------------------------------------------------------------"
  echo "raw LGraph -> Copy-Propagation And Tuple Chain Resolved LGraph"
  echo "--------------------------------------------------------------"
  ${LGSHELL} "lgraph.open name:${pt} |> pass.cprop |> pass.cprop |> pass.cprop |> pass.cprop"
  if [ $? -eq 0 ]; then
    echo "Successfully resolve the tuple chain: inou/pyrope/tests/compiler/${pt}.prp"
  else
    echo "ERROR: Pyrope compiler failed: resolve tuples, testcase: inou/pyrope/tests/compiler/${pt}.prp"
    exit 1
  fi

  ${LGSHELL} "lgraph.open name:${pt} |> inou.graphviz.from"
  mv ${pt}.dot ${pt}.no_bits.dot   
  
  echo "--------------------------------------------------------------"
  echo "LGraph -> Local Bitwidth Optimization(LGraph)             "
  echo "--------------------------------------------------------------"
  
  ${LGSHELL} "lgraph.open name:${pt} |> pass.bitwidth |> pass.bitwidth |> pass.cprop |> pass.bitwidth |> pass.cprop"
  if [ $? -eq 0 ]; then
    echo "Successfully optimize design bitwidth: inou/pyrope/tests/compiler/${pt}.prp"
  else
    echo "ERROR: Pyrope compiler failed: bitwidth optimization, testcase: inou/pyrope/tests/compiler/${pt}.prp"
    exit 1
  fi

  ${LGSHELL} "lgraph.open name:${pt} |> inou.graphviz.from"
  mv ${pt}.dot ${pt}.optimized.dot   
  dot -Tpdf -o ${pt}.optimized.dot.pdf ${pt}.optimized.dot

  echo "--------------------------------------------------------------"
  echo "optimized LGraph -> Verilog_1             "
  echo "--------------------------------------------------------------"
  ${LGSHELL} "lgraph.open name:${pt} |> inou.yosys.fromlg hier:true"
  if [ $? -eq 0 ] && [ -f ${pt}.v ]; then
    echo "Successfully generate Verilog: ${pt}.v"
    rm -f  yosys_script.*
  else
    echo "ERROR: Pyrope compiler failed: verilog generation, testcase: inou/pyrope/tests/compiler/${pt}.prp"
    exit 1
  fi
  mv ${pt}.v ${pt}_1.v

  echo "--------------------------------------------------------------"
  echo "optimized LGraph -> LNAST          "
  echo "--------------------------------------------------------------"
  ${LGSHELL} "lgraph.open name:${pt} |> pass.lnast_fromlg |> lnast.dump"
  if [ $? -eq 0 ]; then
    echo "Successfully obtained LNAST of the optimized LGraph: inou/pyrope/tests/compiler/${pt}.prp"
  else
    echo "ERROR: Pyrope compiler failed: LGraph -> LNAST  conversion, testcase: inou/pyrope/tests/compiler/${pt}.prp"
    exit 1
  fi

  echo "--------------------------------------------------------------"
  echo "optimized LGraph -> LNAST -> pyrope(code_gen)            "
  echo "--------------------------------------------------------------"
  ${LGSHELL} "lgraph.open name:${pt} |> pass.lnast_fromlg |> inou.code_gen.prp odir:prp_v_LEC_test_dir"
  if [ $? -eq 0 ]; then
    echo "Successfully obtained pyrope from LNAST of the optimized LGraph: inou/pyrope/tests/compiler/${pt}.prp"
  else
    echo "ERROR: Pyrope compiler failed: either LNAST or code_gen conversion, testcase: inou/pyrope/tests/compiler/${pt}.prp"
    exit 1
  fi

  echo "--------------------------------------------------------------"
  echo "pyrope(code_gen) -> LNAST -> LGraph"
  echo "--------------------------------------------------------------"
  ${LGSHELL} "inou.pyrope files:prp_v_LEC_test_dir/${pt}.prp |> pass.lnast_tolg path:lgdb2 "
  if [ $? -eq 0 ]; then
    echo "Successfully create the second LGraph: prp_v_LEC_test_dir/${pt}.prp"
  else
    echo "ERROR: Pyrope compiler failed: LNAST -> LGraph, testcase: prp_v_LEC_test_dir/${pt}.prp"
    exit 1
  fi
  
  ${LGSHELL} "lgraph.open name:${pt} path:lgdb2 |> inou.graphviz.from"
  mv ${pt}.dot ${pt}.sec.dot
  dot -Tpdf -o ${pt}.sec.dot.pdf ${pt}.sec.dot

  echo "--------------------------------------------------------------"
  echo "sec LGraph -> Copy-Propagation And Tuple Chain Resolved LGraph"
  echo "--------------------------------------------------------------"
  ${LGSHELL} "lgraph.open name:${pt} path:lgdb2 |> pass.cprop |> pass.cprop |> pass.cprop |> pass.cprop"
  if [ $? -eq 0 ]; then
    echo "Successfully resolve the tuple chain: prp_v_LEC_test_dir/${pt}.prp"
  else
    echo "ERROR: Pyrope compiler failed: resolve tuples, testcase: prp_v_LEC_test_dir/${pt}.prp"
    exit 1
  fi

  ${LGSHELL} "lgraph.open name:${pt} path:lgdb2 |> inou.graphviz.from"   
  
  echo "--------------------------------------------------------------"
  echo "sec LGraph cpropped -> Local Bitwidth Optimization(LGraph)             "
  echo "--------------------------------------------------------------"
  
  ${LGSHELL} "lgraph.open name:${pt} path:lgdb2 |> pass.bitwidth |> pass.bitwidth |> pass.cprop |> pass.bitwidth |> pass.cprop"
  if [ $? -eq 0 ]; then
    echo "Successfully optimize design bitwidth: prp_v_LEC_test_dir/${pt}.prp"
  else
    echo "ERROR: Pyrope compiler failed: bitwidth optimization, testcase: prp_v_LEC_test_dir/${pt}.prp"
    exit 1
  fi

  ${LGSHELL} "lgraph.open name:${pt} path:lgdb2 |> inou.graphviz.from"
  mv ${pt}.dot ${pt}.sec.optimized.dot   
  dot -Tpdf -o ${pt}.sec.optimized.dot.pdf ${pt}.sec.optimized.dot

  echo "--------------------------------------------------------------"
  echo "optimized LGraph -> Verilog_2             "
  echo "--------------------------------------------------------------"
  ${LGSHELL} "lgraph.open name:${pt} path:lgdb2 |> inou.yosys.fromlg hier:true"
  if [ $? -eq 0 ] && [ -f ${pt}.v ]; then
    echo "Successfully generate Verilog: ${pt}.v"
    rm -f  yosys_script.*
  else
    echo "ERROR: Pyrope compiler failed: verilog generation, testcase: prp_v_LEC_test_dir/${pt}.prp"
    exit 1
  fi
  mv ${pt}.v ${pt}_2.v

  echo "--------------------------------------------------------------"
  echo "Logic Equivalence Check"
  echo "--------------------------------------------------------------"
  ${LGCHECK} --implementation=${pt}_2.v --reference=${pt}_1.v

  if [ $? -eq 0 ]; then
    echo "Successfully passed logic equivalence check!"
  else
    echo "FAIL: "${pt}"_1.v !== "${pt}"_2.v"
    exit 1
  fi

done



rm -f lnast.dot.gld
rm -f lnast.nodes
rm -f lnast.nodes.gld
