#!/bin/bash
mv -f lbench.trace lbench.trace.old

# if [ ! -d ./livehd_regression ]; then
#   git clone git@github.com:masc-ucsc/livehd_regression.git

#   cd livehd_regression/synthetic
#   # run the full chisel compilation flow 
#   ./setup.sh 
#   cd ../../
# fi

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

GVIZ='false'


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


pts='Snxn100k'
echo -e "All Benchmark Patterns:" '\n'$pts


fucntion() {
  echo "-------------------- LiveHD  (${FIRRTL_LEVEL}.pb -> Verilog(Cgen)) -----" > stat.livehd

  for pt in $1
  do
    rm -rf $LGDB
    echo ""
    echo ""
    echo ""
    echo "======================================================================"
    echo "                     LiveHD Full Compilation: ${pt}.${FIRRTL_LEVEL}.pb"
    echo "======================================================================"
    # livehd compilation
    if [ ! -f ${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.pb ]; then
        echo "ERROR: could not find ${pt}.${FIRRTL_LEVEL}.pb in ${PATTERN_PATH}"
        exit 1
    fi 
    # perf record --call-graph fp ${LGSHELL} "inou.firrtl.tolnast path:${LGDB} files:${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.pb |> pass.compiler gviz:${GVIZ} top:${pt} firrtl:true |> inou.cgen.verilog" 

    perf stat -o pp ${LGSHELL} "inou.firrtl.tolnast path:${LGDB} files:${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.pb |> pass.compiler gviz:${GVIZ} top:${pt} firrtl:true"

    ret_val=$?
    if [ $ret_val -ne 0 ]; then
      echo "ERROR: could not compile with pattern: ${pt}.${FIRRTL_LEVEL}.pb!"
      exit $ret_val
    fi

    grep elapsed pp       >> stat.livehd
  done #end of for


  cat stat.livehd

  # rm -f *.dot
  # rm -f *.v
  rm -f ./*.tcl
  rm -f pp*
}

fucntion "$pts"
