#!/bin/sh
rm -rf ./lgdb
rm -rf ./mtlogs

mkdir -p mtlogs/yosys_log

### Don't need LUT-synth
# pts='wires simple_rf2 simple_flop simple_add satsmall satpick satlarge
#      params nlatch mem3 flop async assigns arith add'


### wait for hierarchical traversal
# pts='trivial2 unconnected hierarchy submodule submodule_offset punching
#      punching_3 params_submodule paramods join_fadd common_sub arraycells'


### sh:todo
# pts='dce2 dce3 compare2 offset operators consts not_vslogicnot
#      mux mismatch expression_00002
#      graphtest kogg_stone_64 test simple_weird
#      shift shiftx_simple shiftx shared_ports
#      long_gcd long_simple_rf1 long_regfile1r1w'


pts='trivial_offset trivial2a trivial trivial3 trivial_and
     dce1 gates trivial1 trivial_join compare cse_basic
     simple_weird2 mt_basic_test reduce null_port
     '

PTS_PATH="inou/yosys/tests"

if [ "$1" != "" ]; then
  PTS_PATH=""
  pts=""
  while [ "$1" != "" ]; do
    full_input=$1
    shift

    PTS_PATH=$(dirname ${full_input})
    base=$(basename ${full_input})
    TT=${base%.*}
    pts=${pts}" "${TT}
    echo ${TT}
    echo $base
    echo $PTS_PATH
  done
  echo "mt_test.sh inputs: ${pts}"
fi

pts_fixme='shift'

LGSHELL=./bazel-bin/main/lgshell
LGCHECK=./inou/yosys/lgcheck


if [ ! -f $LGSHELL ]; then
  if [ -f ./main/lgshell ]; then
    LGSHELL=./main/lgshell
    echo "lgshell is in $(pwd)"
  else
    echo "FAILED: could not find lgshell binary in $(pwd)";
  fi
fi

for pt in $pts
do
  echo "Pattern:${PTS_PATH}/${pt}.v"
  echo ""
  echo "Mockturtle LUT Synthesis Flow"
  echo ""
  ${LGSHELL} "inou.yosys.tolg files:${PTS_PATH}/${pt}.v"
  ${LGSHELL} "lgraph.open name:${pt}          |> inou.graphviz.from"
  ${LGSHELL} "lgraph.open name:${pt}          |> pass.mockturtle"
  ${LGSHELL} "lgraph.open name:${pt}_lutified |> inou.yosys.fromlg odir:mtlogs"
  ${LGSHELL} "lgraph.open name:${pt}_lutified |> inou.graphviz.from odir:mtlogs"

  if [ $? -eq 0 ] && [ -f mtlogs/${pt}_lutified.v ]; then
    echo "Successfully created lutified verilog:${pt}_lutified.v"
  else
    echo "FAIL: verilog generation terminated with an error, testcase: ${pt}.v"
    exit 1
  fi

  echo ""
  echo "Logic Equivalence Check"
  echo ""

  ${LGCHECK} -r ${PTS_PATH}/${pt}.v -i ./mtlogs/${pt}_lutified.v
  if [ $? -eq 0 ]; then
    echo "Successfully pass logic equivilence check!"
    echo "=========================================="
    echo "=========================================="
    echo ""
  else
    echo "FAIL: $pt.v !== $pt""_gld.v"
    exit 1
  fi

done

exit 0

