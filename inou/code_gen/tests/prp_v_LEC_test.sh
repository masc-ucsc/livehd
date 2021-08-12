#!/bin/bash
rm -rf ./lgdb
rm -rf ./lgdb2
rm -rf ./prp_v_LEC_test_dir
rm -f ./*dot*
rm -f ./*.v
rm -f lnast.dot.gld
rm -f lnast.nodes
rm -f lnast.nodes.gld

pts='logic'
pts1='logic1 if bits_rhs'
pts_hier='sum funcall'
pts_hier2='sum2 funcall2'    
#inline function call        
pts_hier4='funcall4'         
pts_hier5='funcall5'         
pts_hier6='funcall_unnamed' 

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
Pyrope_lec_test () {
  echo "=============================================================="
  echo "Flow:                                                         "
  echo "  Prp_in -> LN -> LG -> LN -> Prp_out                         "
  echo "                   |               |                          "
  echo "                   V               V                          "
  echo "                  Ver_1           LN -> LG -> Ver_2           "
  echo "  Perform LEC b/w Ver_1 and Ver_2                             "
  echo "=============================================================="


  for pt in $1
  #for pt in $pts
  do
    #checking whether the file exists or not
    if [ ! -f inou/pyrope/tests/compiler/${pt}.prp ]; then
      echo "ERROR: could not find ${pt}.prp in /inou/pyrope/tests/compiler"
      exit 2
    fi

    echo "--------------------------------------------------------------"
    echo "PRP -> LNAST -> LGraph                                        "
    echo "--------------------------------------------------------------"
    ${LGSHELL} "inou.pyrope files:inou/pyrope/tests/compiler/${pt}.prp |> lnast.dump |> pass.lnast_tolg"
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
  done


  if [[ $2 == "hier" ]]; then
    #get the last pattern of pts_hier as the top module
    top_module=$(echo $1 | awk '{print $NF}')
    echo $top_module

    echo "----------------------------------------------------"
    echo "Hierarchical Bitwidth Optimization(LGraph)"
    echo "----------------------------------------------------"
    ${LGSHELL} "lgraph.open name:${top_module} |> pass.bitwidth hier:true |> pass.bitwidth hier:true"
    if [ $? -eq 0 ]; then
      echo "Successfully optimize hier-design bitwidth: inou/pyrope/tests/compiler/${top_module}.prp"
    else
      echo "ERROR: Pyrope compiler failed: hier-bitwidth optimization, testcase: inou/pyrope/tests/compiler/${top_module}.prp"
      exit 1
    fi

    ${LGSHELL} "lgraph.open name:${top_module} |> inou.graphviz.from verbose:false"
    mv ${top_module}.dot ${top_module}.hier.dot
  fi # end of hier bits

  for pt in $1
  do
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
    ${LGSHELL} "lgraph.open name:${pt} |> pass.lnast_fromlg |> lnast.dump |> inou.code_gen.prp odir:prp_v_LEC_test_dir"
    if [ $? -eq 0 ]; then
      echo "Successfully obtained pyrope from LNAST of the optimized LGraph: inou/pyrope/tests/compiler/${pt}.prp"
    else
      echo "ERROR: Pyrope compiler failed: either LNAST or code_gen conversion, testcase: inou/pyrope/tests/compiler/${pt}.prp"
      exit 1
    fi
	done

  for pt in $1
  do
    if [[ ${pt} == *_err* ]]; then
      echo "----------------------------------------------------"
      echo "Pass! This is a Compile Error Test, No Need to Generate Verilog Code "
      echo "----------------------------------------------------"
    else
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
      $(cat ${pt}_1.v >> ${top_module}_1.v)
     fi
    done
  fi

#//////:to here

	for pt in $1
	do
    echo "--------------------------------------------------------------"
    echo "pyrope(code_gen) -> LNAST -> LGraph"
    echo "--------------------------------------------------------------"
    ${LGSHELL} "inou.pyrope files:prp_v_LEC_test_dir/${pt}.prp |> lnast.dump |> pass.lnast_tolg path:lgdb2 "
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
  done

  #logic equivalence check
  if [[ $2 == "hier" ]]; then
    #get the last pattern of pts_hier
    top_module=$(echo $1 | awk '{print $NF}')
    echo $top_module

    #concatenate every submodule under top_module.v
    for pt in $1
    do
     if [[ pt != $top_module ]]; then
      $(cat ${pt}_2.v >> ${top_module}_2.v)
     fi
    done

    echo "----------------------------------------------------"
    echo "Logic Equivalence Check: Hierarchical Design"
    echo "----------------------------------------------------"

    ${LGCHECK} --top $top_module --implementation ${top_module}_2.v --reference ${top_module}_1.v

    if [ $? -eq 0 ]; then
      echo "Successfully pass logic equivilence check!"
    else
      echo "FAIL: ${top_module}_1.v !== ${top_module}_2.v"
      exit 1
    fi

  else
    for pt in $1
    do
      echo "--------------------------------------------------------------"
      echo "Logic Equivalence Check"
      echo "--------------------------------------------------------------"
      ${LGCHECK} --implementation ${pt}_2.v --reference ${pt}_1.v

      if [ $? -eq 0 ]; then
        echo "Successfully passed logic equivalence check!"
      else
        echo "FAIL: ${pt}_1.v !== ${pt}_2.v"
        exit 1
      fi
    done
  fi
}

Pyrope_lec_test "$pts1"
Pyrope_lec_test "$pts_hier" "hier"
#Pyrope_lec_test "$pts_hier2" "hier" 
#Pyrope_lec_test "$pts_hier4" "hier" 
                                   
                                   
rm -f lnast.dot.gld
rm -f lnast.nodes
rm -f lnast.nodes.gld
