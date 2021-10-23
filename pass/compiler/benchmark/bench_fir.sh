#!/bin/bash
if [ -f "./lbench.trace" ]; then
  mv -f lbench.trace lbench.trace.old
fi

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
  # LGDB=./lgdb
  LGDB=/local/scrap/masc/swang203/lgdb
else
  LGDB=${MADA_SCRAP}/lgdb   # NSF
fi

# LGDB=/local/scrap/masc/swang203/lgdb
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


pts='Snxn100k Snxn200k Snxn300k Snxn400k Snxn500k Snxn600k Snxn700k Snxn800k Snxn900k Snxn1000k'
pts='Snxn1000k'
echo -e "All Benchmark Patterns:" '\n'$pts


fucntion() {

  for pt in $1
  do
    echo "-------------------- LiveHD  $pt Compilation with $2 Threads -----" >> stat.livehd
    rm -rf $LGDB
    # echo ""
    # echo ""
    # echo ""
    # echo "======================================================================"
    # echo "                     LiveHD Full Compilation: ${pt}.${FIRRTL_LEVEL}.pb"
    # echo "======================================================================"
    # livehd compilation
    if [ ! -f ${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.pb ]; then
        echo "ERROR: could not find ${pt}.${FIRRTL_LEVEL}.pb in ${PATTERN_PATH}"
        exit 1
    fi
    # perf record --call-graph fp ${LGSHELL} "inou.firrtl.tolnast path:${LGDB} files:${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.pb |> pass.compiler gviz:${GVIZ} top:${pt} firrtl:true |> inou.cgen.verilog"

    # perf stat -o pp ${LGSHELL} "inou.firrtl.tolnast path:${LGDB} files:${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.pb |> pass.compiler gviz:${GVIZ} top:${pt} firrtl:true"
    perf stat -o pp ${LGSHELL} "inou.firrtl.tolnast path:${LGDB} files:${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.pb |> pass.compiler gviz:${GVIZ} top:${pt} firrtl:true |> lgraph.open path:${LGDB} name:${pt} hier:true |> inou.cgen.verilog odir:${LGDB}/gen_verilog"

    ret_val=$?
    if [ $ret_val -ne 0 ]; then
      echo "ERROR: could not compile with pattern: ${pt}.${FIRRTL_LEVEL}.pb!"
      exit $ret_val
    fi

    grep elapsed pp >> stat.livehd
    cp pp perf.livehd

  # # Verilog code generation
    # perf stat -o pp ${LGSHELL} "lgraph.open path:${LGDB} name:${pt} hier:true |> inou.cgen.verilog odir:tmp_firrtl"
    # grep elapsed pp >> stat.livehd

    # ret_val=$?
    # if [ $ret_val -eq 0 ] && [ -f "tmp_firrtl/${pt}.v" ]; then
    #     echo "Successfully generate Verilog: tmp_firrtl/top_${pt}.v"
    #     rm -f  yosys_script.*
    # else
    #     echo "ERROR: Firrtl compiler failed: verilog generation, testcase: ${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.pb"
    #     exit $ret_val
    # fi

  done #end of for


  # rm -f *.dot
  # rm -f *.v
  rm -f ./*.tcl
  rm -f pp
}



echo "-------------------- Benchmark Start -----" > stat.livehd
# for thds in 2 3 4 8 16 32
# note: for single thread, you have to disable thread pool directly
# for thds in 1
# set up $1 from the command line, ex: ./bench_fir.sh '1 2 3 4 8 16'
for thds in $1
do
  export LIVEHD_THREADS=$thds
  fucntion "$pts" "$thds"
done



