#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#declare -a inputs=("trivial.v" "null_port.v" "simple_flop.v" "test.v" "shift.v"\
#                   "wires.v" "reduce.v" "graphtest.v" "add.v"  "assigns.v" \
#                   "submodule.v"\
#                   "gcd.v" "common_sub.v" "trivial2.v" "consts.v" "async.v"\
#                   "unconnected.v" "gates.v" "operators.v" \
#                   #"shiftx.v" "regfile2r1w.v" \  #cases currently not working
#                   "offset.v" "submodule_offset.v" "mem.v" "mem2.v" \
#                   "params.v" "params_submodule.v" "iwls_adder.v")

declare -a inputs=("trivial.v" "null_port.v" "simple_flop.v" "test.v" "shift.v"\
                   "simple_add.v" \
                  "wires.v" "reduce.v" "add.v"  "assigns.v")

TEMP=$(getopt -o ps:: --long profile,source:: -n 'yosys.sh' -- "$@")
eval set -- "$TEMP"

YOSYS=./inou/yosys/lgyosys
LGCHECK=./inou/yosys/lgcheck
OPT_LGRAPH="./"
while true ; do
    case "$1" in
        -p|--profile)
          shift
          YOSYS="./inou/yosys/lgyosys --profile"
          ;;
        -s|--source)
            case "$2" in
                "") shift 2 ;;
                *) OPT_LGRAPH=$2;
                shift 2 ;;
            esac ;;
        --)
          shift
          break
          ;;
        *)
          echo "Option $1 not recognized!"
          exit 1
          ;;
    esac
done


YOSYS_BIN=$(which yosys)
YOSYS_LIB="/usr/local/share/yosys/"

if [ ! -f ${YOSYS_BIN} ]; then
  echo "ERROR: could not find yosys binary in path"
  exit 1
fi

if [ ! -d ${YOSYS_LIB} ]; then
  echo "ERROR: could not find yosys library files installed"
  exit 1
fi

if [ -z "${OPT_LGRAPH}" ] ; then
  echo "ERROR: -s|--source required and needs to point to lgraph root"
  exit 1
fi

if [ ! -d "${OPT_LGRAPH}" ]; then
  echo "Lgraph not found on ${OPT_LGRAPH}"
  exit 1
fi

if [ ! -f "${OPT_LGRAPH}/inou/tech/verilog_json.rb" ]; then
  echo "verilog.rb not found on $(pwd)/inou/tech/"
  exit 1
fi

OPT_INOU_YOSYS=""
if [ -f "${OPT_INOU_YOSYS}" ]; then
  echo ""
elif [ -d "bazel-bin" ] && [ -f "./bazel-bin/inou/yosys/liblgraph_yosys.so" ]; then
  OPT_INOU_YOSYS="./bazel-bin/inou/yosys/liblgraph_yosys.so"
elif [ -f "./inou/yosys/liblgraph_yosys.so" ]; then
  OPT_INOU_YOSYS="./inou/yosys/liblgraph_yosys.so"
else
  echo "Could not find liblgraph_yosys.so library in ${OPT_INOU_YOSYS}"
  exit 3
fi

rm -rf ./lgdb/ ./logs ./synth-test ./*.json ./*.v
mkdir synth-test/
mkdir logs
mkdir lgdb

ruby ${OPT_LGRAPH}/inou/tech/verilog_json.rb \
  ${YOSYS_LIB}/simcells.v  \
  ${YOSYS_LIB}/xilinx/cells_sim.v \
  ${YOSYS_LIB}/xilinx/brams_bb.v > lgdb/tech_library

for input in ${inputs[@]}
do

  base=${input%.*}
  synth_script="hierarchy -auto-top; clean; proc_arst; proc; opt -fast; pmuxtree; memory -nomap;
  hierarchy -check -auto-top; proc; flatten; synth -run coarse;
  memory_bram -rules +/xilinx/brams.txt; techmap -map +/xilinx/brams_map.v; memory_bram -rules +/xilinx/drams.txt; techmap -map +/xilinx/drams_map.v;
  opt -fast -full; memory_map; dffsr2dff; dff2dffe; opt -full;
  techmap -map +/techmap.v -map +/xilinx/arith_map.v; opt -fast; techmap -D ALU_RIPPLE;
  opt -fast; abc -D 100;"
  yosys -m ${OPT_INOU_YOSYS} -p "read_verilog -sv ${OPT_LGRAPH}/inou/yosys/tests/${input};
   ${synth_script}; write_verilog ${base}_synth.v; yosys2lg -path lgdb" > ./synth-test/log_from_yosys_${input} 2> ./synth-test/err_from_yosys_${input}


  if [ $? -eq 0 ]; then
    echo "Successfully created graph from ${input}"
  else
  echo "gdb --args yosys -m ${OPT_INOU_YOSYS} -p \"read_verilog -sv ${OPT_LGRAPH}/inou/yosys/tests/${input};
    ${synth_script};  write_verilog ${input}_synth.v; yosys2lg -path lgdb\""
    echo "FAIL: lgyosys parsing terminated with an error (testcase ${input})"
    exit 1
  fi

  #./inou/json/lgjson  --graph_name ${base} --json_output ${base}.json > ./synth-test/log_json_${input} 2> ./synth-test/err_json_${input}
  #if [ $? -ne 0 ]; then
    #echo "WARN: Not able to create json for testcase ${input}"
  #fi

  ${YOSYS} -g${base} -h > ./synth-test/log_to_yosys_${input} 2> ./synth-test/err_to_yosys_${input}

  if [ $? -eq 0 ]; then
    echo "Successfully created verilog from graph ${input}"
  else
    echo "${YOSYS} -g${base} -h -d"
    echo "FAIL: verilog generation terminated with an error (testcase ${input})"
    exit 1
  fi

  ${LGCHECK} --implementation=${base}.v --reference=${OPT_LGRAPH}/inou/yosys/tests/${base}.v -l${YOSYS_LIB}/xilinx/cells_sim.v
  if [ $? -eq 0 ]; then
    echo "Successfully matched generated verilog with original verilog (${input})"
  else
    echo "FAIL: circuits are not equivalent (${input})"
    exit 1
  fi

done

echo "SUCCESS: all yosys test cases ended without errors"

