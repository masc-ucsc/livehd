#!/bin/bash
mv -f lbench.trace lbench.trace.old

if [ ! -d ./livehd_regression ]; then
  git clone git@github.com:masc-ucsc/livehd_regression.git

  cd livehd_regression/synthetic
  ./setup.sh
  cd ../../
fi

FIRRTL_LEVEL='ch'
LGSHELL=./bazel-bin/main/lgshell
LGCHECK=./inou/yosys/lgcheck
POST_IO_RENAME=./inou/firrtl/post_io_renaming.py
PATTERN_PATH=./livehd_regression/synthetic/generated
FIRRTL_EXE=./livehd_regression/tools/firrtl/utils/bin/firrtl

if [ "${PWD##/home/}" != "${PWD}" ]; then
  LGDB=./lgdb
else
  LGDB=/local/scrap/masc/swang203/lgdb   # NSF
fi

GVIZ='true'
INSTANCES='16'
# INSTANCES='256'

rm -rf $LGDB

if [ ! -f $LGSHELL ]; then
  if [ -f ./main/lgshell ]; then
    LGSHELL=./main/lgshell
    echo "lgshell is in $(pwd)"
  else
    echo "ERROR: could not find lgshell binary in $(pwd)";
  fi
fi


unsorted=''
for filename in ./livehd_regression/synthetic/generated/*${INSTANCES}.${FIRRTL_LEVEL}.pb
do
  pt=$(basename "$filename" .${FIRRTL_LEVEL}.pb) # ./foo/bar.scala -> bar 
  unsorted+="$pt "
done

#bash natural sort
pts=$(echo $unsorted | tr " " "\n" | sort -V)




pts='Snx64Insts16'
echo -e "All Benchmark Patterns:" '\n'$pts


fucntion() {
  echo "-------------------- LiveHD  (${FIRRTL_LEVEL}.pb -> Verilog(Cgen)) -----" > stat.livehd
  echo "-------------------- LiveHD  (${FIRRTL_LEVEL}.pb -> Verilog(Yosys)) ----" > stat.livehd-yosys
  echo "-------------------- FIRRTL  (Chirrtl -> Verilog) ---------"              > stat.fir2verilog

  for pt in $1
  do
    echo ""
    echo ""
    echo ""
    echo "======================================================================"
    echo "                     LiveHD ${FIRRTL_LEVEL}.pb Full Compilation: ${pt}.pb"
    echo "======================================================================"
    # livehd compilation
    if [ ! -f ${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.pb ]; then
        echo "ERROR: could not find ${pt}.${FIRRTL_LEVEL}.pb in ${PATTERN_PATH}"
        exit 1
    fi 

    # perf record --call-graph fp ${LGSHELL} "inou.firrtl.tolnast path:${LGDB} files:${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.pb |> pass.compiler gviz:${GVIZ} top:${pt} firrtl:true |> inou.cgen.verilog" 

    # perf stat -o pp ${LGSHELL} "inou.firrtl.tolnast path:${LGDB} files:${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.pb |> pass.compiler gviz:${GVIZ} top:${pt} firrtl:true |> inou.cgen.verilog"
    perf stat -o pp ${LGSHELL} "inou.firrtl.tolnast path:${LGDB} files:${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.pb |> pass.compiler gviz:${GVIZ} top:${pt} firrtl:true"

    # perf stat -o pp-yosys ${LGSHELL} "inou.firrtl.tolnast path:${LGDB} files:${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.pb 
    #                             |> pass.compiler gviz:${GVIZ} top:${pt} firrtl:true 
    #                             |> inou.yosys.fromlg hier:true" 

    ret_val=$?
    if [ $ret_val -ne 0 ]; then
      echo "ERROR: could not compile with pattern: ${pt}.${FIRRTL_LEVEL}.pb!"
      exit $ret_val
    fi


    # echo ""
    # echo ""
    # echo ""
    # echo "======================================================================"
    # echo "                     FIRRTL Compilation from Chirrtl: ${pt}.fir"
    # echo "======================================================================"

    # # firrtl compilation
    # if [ ! -f ${PATTERN_PATH}/${pt}.fir ]; then
    #   echo "ERROR: could not find ${pt}.fir in ${PATTERN_PATH}"
    #   exit 1
    # else
    #   # echo $pt
    #   # perf stat -o pp2 $FIRRTL_EXE -i   ${PATTERN_PATH}/${pt}.fir -X verilog

    #   mv perf.data perf.data.${pt}
    #   mv perf.data.old perf.data.old.${pt}
    #   echo "      ${pt}"    >> stat.livehd
      grep elapsed pp       >> stat.livehd
    #   echo "      ${pt}"    >> stat.livehd-yosys
    #   grep elapsed pp-yosys >> stat.livehd-yosys
    #   echo "      ${pt}"    >> stat.fir2verilog
    #   grep elapsed pp2      >> stat.fir2verilog
    # fi
    
    # note: Chisel->ch.pb is recorded during synthetic pattern generation
  done #end of for


  # cat stat.chisel3-full >  stat.summary
  # cat stat.chisel3-pb   >> stat.summary
  # cat stat.livehd-yosys >> stat.summary
  # cat stat.livehd       >> stat.summary
  # cat stat.fir2verilog  >> stat.summary
  # cat stat.chisel3-fir  >> stat.summary
  # cat stat.summary
  cat stat.livehd

  # rm -f *.dot
  # rm -f *.v
  rm -f ./*.tcl
  rm -f pp*
}

fucntion "$pts"
