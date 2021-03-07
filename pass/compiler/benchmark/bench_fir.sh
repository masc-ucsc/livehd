#!/bin/bash
mv -f lbench.trace lbench.trace.old

if [ ! -d ./livehd_regression ]; then
  git clone git@github.com:masc-ucsc/livehd_regression.git

  cd livehd_regression/synthetic
  ./setup.sh
  cd ../../
fi

FIRRTL_LEVEL='hi'
LGSHELL=./bazel-bin/main/lgshell
LGCHECK=./inou/yosys/lgcheck
POST_IO_RENAME=./inou/firrtl/post_io_renaming.py
PATTERN_PATH=./livehd_regression/synthetic/generated
FIRRTL_EXE=./livehd_regression/tools/firrtl/utils/bin/firrtl
LGDB=/local/scrap/masc/swang203/lgdb
GVIZ='false'

rm -rf $LGDB
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


pts='Snx1024Insts256'

echo -e "All Benchmark Patterns:" '\n'$pts


firrtl_test() {
  echo "-------------------- Chirrtl -> hi.pb --------------------" > stat.protogen
  echo "-------------------- LiveHD (hi.pb -> Verilog(Cgen)) -----" > stat.livehd
  echo "-------------------- LiveHD (hi.pb -> Verilog(Yosys)) ----" > stat.livehd-yosys
  echo "-------------------- FIRRTL (Chirrtl -> Verilog) ---------" > stat.firrtl



  echo ""
  echo ""
  echo ""
  echo "======================================================================"
  echo "                         hiFIRRTL Full Compilation"
  echo "======================================================================"
  for pt in $1
  do
    # livehd compilation
    if [ ! -f ${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.pb ]; then
        echo "ERROR: could not find ${pt}.${FIRRTL_LEVEL}.pb in ${PATTERN_PATH}"
        exit 1
    fi 

    perf record --call-graph fp ${LGSHELL} "inou.firrtl.tolnast path:${LGDB} files:${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.pb 
                                |> pass.compiler gviz:${GVIZ} top:${pt} firrtl:true 
                                |> inou.cgen.verilog" 

    perf stat -o pp ${LGSHELL} "inou.firrtl.tolnast path:${LGDB} files:${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.pb 
                                |> pass.compiler gviz:${GVIZ} top:${pt} firrtl:true 
                                |> inou.cgen.verilog"
    perf stat -o pp-yosys ${LGSHELL} "inou.firrtl.tolnast path:${LGDB} files:${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.pb 
                                |> pass.compiler gviz:${GVIZ} top:${pt} firrtl:true 
                                |> inou.yosys.fromlg hier:true" 

    ret_val=$?
    if [ $ret_val -ne 0 ]; then
      echo "ERROR: could not compile with pattern: ${pt}.${FIRRTL_LEVEL}.pb!"
      exit $ret_val
    fi



    # firrtl compilation
    if [ ! -f ${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.fir ]; then
      echo "ERROR: could not find ${pt}.${FIRRTL_LEVEL}.fir in ${PATTERN_PATH}"
      exit 1
    else
      echo $pt
      echo "---------- Chirrtl Compilation: $pt.hi.fir ----------"
      echo "-------- FIRRTL Compilation Time --------" > pp_firrtl
      perf stat -o pp2 $FIRRTL_EXE -i   ${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.fir -X verilog
      perf stat -o pp3 $FIRRTL_EXE -i   ${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.fir -X none --custom-transforms firrtl.transforms.WriteHighPB

      echo "      ${pt}"    >> stat.protogen
      grep elapsed pp3      >> stat.protogen
      echo "      ${pt}"    >> stat.livehd
      grep elapsed pp       >> stat.livehd
      echo "      ${pt}"    >> stat.livehd-yosys
      grep elapsed pp-yosys >> stat.livehd-yosys
      echo "      ${pt}"    >> stat.firrtl
      grep elapsed pp2      >> stat.firrtl

      cat stat.protogen
      cat stat.livehd-yosys
      cat stat.livehd
      cat stat.firrtl
    fi
  done #end of for





  # for pt in $1
  # do
  #   # compile only if foo.scala is not exists
  #   if [ ! -f ${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.fir ]; then
  #     echo "ERROR: could not find ${pt}.${FIRRTL_LEVEL}.fir in ${PATTERN_PATH}"
  #     exit 1
  #   else
  #     echo $pt
  #     echo "---------- Chirrtl Compilation: $pt.hi.fir ----------"
  #     echo "-------- FIRRTL Compilation Time --------" > pp_firrtl
  #     perf stat -o pp2 $FIRRTL_EXE -i   ${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.fir -X verilog
  #     perf stat -o pp3 $FIRRTL_EXE -i   ${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.fir -X none --custom-transforms firrtl.transforms.WriteHighPB

  #     echo "      ${pt}" >> stat.protogen
  #     grep elapsed pp3 >> stat.protogen
  #     echo "      ${pt}" >> stat.livehd
  #     grep elapsed pp >> stat.livehd
  #     echo "      ${pt}" >> stat.livehd-yosys
  #     grep elapsed pp-yosys >> stat.livehd-yosys
  #     echo "      ${pt}" >> stat.firrtl
  #     grep elapsed pp2 >> stat.firrtl

  #     cat stat.protogen
  #     cat stat.livehd-yosys
  #     cat stat.livehd
  #     cat stat.firrtl
  #   fi
  # done


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

  #   ${LGCHECK} --implementation=${pt}.v --reference=${PATTERN_PATH}/${pt}.v

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
