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
LGDB=/local/scrap/masc/swang203/lgdb
GVIZ='true'

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
for filename in ./livehd_regression/synthetic/generated/*.${FIRRTL_LEVEL}.pb
do
  pt=$(basename "$filename" .${FIRRTL_LEVEL}.pb) # ./foo/bar.scala -> bar 
  unsorted+="$pt "
done

#bash natural sort
pts=$(echo $unsorted | tr " " "\n" | sort -V)
echo $pts


pts='Snx1024Insts256'
pts='Snx512Insts128'
pts='Snx256Insts64'
pts='Snx128Insts32'
# pts='Snx64Insts16'
# pts='Snx32Insts8'
# pts='Snx16Insts4'
# pts='Snx8Insts2'


echo -e "All Benchmark Patterns:" '\n'$pts


fucntion() {
  # echo "-------------------- Chisel3 (Chisel -> ${FIRRTL_LEVEL}.pb)-------------" > stat.chiesel3-pb
  echo "-------------------- LiveHD  (${FIRRTL_LEVEL}.pb -> Verilog(Cgen)) -----" > stat.livehd
  echo "-------------------- LiveHD  (${FIRRTL_LEVEL}.pb -> Verilog(Yosys)) ----" > stat.livehd-yosys
  echo "-------------------- FIRRTL  (Chirrtl -> Verilog) ---------"              > stat.firrtl

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

    # perf record --call-graph fp ${LGSHELL} "inou.firrtl.tolnast path:${LGDB} files:${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.pb 
    #                             |> pass.compiler gviz:${GVIZ} top:${pt} firrtl:true 
    #                             |> inou.cgen.verilog" 

    perf stat -o pp ${LGSHELL} "inou.firrtl.tolnast path:${LGDB} files:${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.pb 
                                |> pass.compiler gviz:${GVIZ} top:${pt} firrtl:true 
                                |> inou.cgen.verilog"
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
    #   echo $pt
    #   perf stat -o pp2 $FIRRTL_EXE -i   ${PATTERN_PATH}/${pt}.fir -X verilog

    #   echo "      ${pt}"    >> stat.livehd
    #   grep elapsed pp       >> stat.livehd
    #   echo "      ${pt}"    >> stat.livehd-yosys
    #   grep elapsed pp-yosys >> stat.livehd-yosys
    #   echo "      ${pt}"    >> stat.firrtl
    #   grep elapsed pp2      >> stat.firrtl
    # fi


    # echo ""
    # echo ""
    # echo ""
    # echo "======================================================================"
    # echo "                     Chisel3 Protobuf Compilation : ${pt}.scala"
    # echo "======================================================================"
    # if [ ! -f ${PATTERN_PATH}/${pt}.scala ]; then
    #   echo "ERROR: could not find ${pt}.scala in ${PATTERN_PATH}"
    #   exit 1
    # else
    #   rm -f livehd_regression/fir_regression/chisel_bootstrap/src/main/scala/*.scala
    #   cp ${PATTERN_PATH}/${pt}.scala  livehd_regression/fir_regression/chisel_bootstrap/src/main/scala/
    #   pushd .
    #   cd livehd_regression/fir_regression/chisel_bootstrap

    #   # CHIRRTL PB
    #   sbt "runMain chisel3.stage.ChiselMain --no-run-firrtl --chisel-output-file ${pt}.ch.pb --module ${pt}.${pt}" > pp3
      

    #   echo "      ${pt}"      >> ../../../stat.chiesel3-pb
    #   grep "Total time" pp3   >> ../../../stat.chiesel3-pb
    #   rm -f ${pt}.ch.pb
    #   rm -f ${pt}.anno.json
    #   popd
    # fi
  done #end of for


  # cat stat.chisel3-pb   >  stat.summary
  # cat stat.livehd-yosys >> stat.summary
  # cat stat.livehd       >> stat.summary
  # cat stat.firrtl       >> stat.summary
  # cat stat.chisel3-full >> stat.summary
  # cat stat.summary

  # rm *.v
}

fucntion "$pts"
