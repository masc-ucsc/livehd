#!/bin/bash
rm -rf ./lgdb
# FIRRTL_LEVEL='lo'
FIRRTL_LEVEL='hi'


if [ ! -d ./livehd_regression ]; then
  git clone git@github.com:masc-ucsc/livehd_regression.git

  cd livehd_regression/synthetic
  ./setup.sh
  cd ../../
fi


LGSHELL=./bazel-bin/main/lgshell
LGCHECK=./inou/yosys/lgcheck
POST_IO_RENAME=./inou/firrtl/post_io_renaming.py
PATTERN_PATH=./livehd_regression/synthetic/generated

if [ ! -f $LGSHELL ]; then
    if [ -f ./main/lgshell ]; then
        LGSHELL=./main/lgshell
        echo "lgshell is in $(pwd)"
    else
        echo "ERROR: could not find lgshell binary in $(pwd)";
    fi
fi

pts=''
for filename in ./livehd_regression/synthetic/generated/*.hi.pb
do
  pt=$(basename "$filename" .hi.pb) # ./foo/bar.scala -> bar 
  pts+="$pt "
done


pts='Xor8000Thread64'

echo -e "All Benchmark Patterns:" '\n'$pts


firrtl_test() {
  echo ""
  echo ""
  echo ""
  echo "======================================================================"
  echo "                         hiFIRRTL Full Compilation"
  echo "======================================================================"
  for pt in $1
  do
    if [ ! -f ${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.pb ]; then
        echo "ERROR: could not find ${pt}.${FIRRTL_LEVEL}.pb in ${PATTERN_PATH}"
        exit 1
    fi 

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

    ${LGSHELL} "lgraph.open name:${pt} |> inou.yosys.fromlg hier:true"
    # ${LGSHELL} "lgraph.open name:${pt} |> inou.yosys.fromlg"
    if [ $? -eq 0 ] && [ -f ${pt}.v ]; then
        echo "Successfully generate Verilog: ${pt}.v"
        rm -f  yosys_script.*
    else
        echo "ERROR: Firrtl compiler failed: verilog generation, testcase: ${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.pb"
        exit 1
    fi
  done


  # # Logic Equivalence Check
  # for pt in $1
  # do
  #   echo ""
  #   echo ""
  #   echo ""
  #   echo "----------------------------------------------------"
  #   echo "Logic Equivalence Check"
  #   echo "----------------------------------------------------"
    
  #   if [ "${FIRRTL_LEVEL}" == "hi" ]; then
  #       python3 ${POST_IO_RENAME} "${pt}.v"
  #   fi

  #   ${LGCHECK} --implementation=${pt}.v --reference=./inou/firrtl/tests/verilog_gld/${pt}.gld.v

  #   if [ $? -eq 0 ]; then
  #     echo "Successfully pass LEC!"
  #   else
  #       echo "FAIL: "${pt}".v !== "${pt}".gld.v"
  #       exit 1
  #   fi
  # done

  # rm -f *.v
  # rm -f *.dot
  # rm -f *.tcl
  # rm -f lgcheck*
  # rm -rf lgdb
}

firrtl_test "$pts"
