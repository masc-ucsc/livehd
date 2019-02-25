#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

declare -a inputs=("dce1.v" "trivial.v" "null_port.v" "simple_flop.v" "test.v" "shift.v"\
                   "simple_add.v" \
                   "wires.v" "reduce.v" "graphtest.v" "add.v"  "assigns.v" \
                   "submodule.v" "multiport.v"\
                   "gcd.v" "common_sub.v" "trivial2.v" "consts.v" "async.v"\
                   "unconnected.v" "gates.v" "operators.v" \
                   #"shiftx.v" "regfile2r1w.v" \  #cases currently not working
                   "offset.v" "submodule_offset.v" "mem.v" "mem2.v" \
                    # "mem_offset.v" \ #deactivated until we get the new yosys
                   "params.v" "params_submodule.v" "iwls_adder.v")

TEMP=$(getopt -o p --long profile -n 'yosys.sh' -- "$@")
eval set -- "$TEMP"

echo "yosys.sh running in "$(pwd)

YOSYS=./inou/yosys/lgyosys
LGCHECK=./inou/yosys/lgcheck
while true ; do
    case "$1" in
        -p|--profile)
          shift
          YOSYS="${YOSYS} --profile"
          ;;
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

rm -rf ./lgdb/ ./logs ./yosys-test ./*.v ./*.json
mkdir yosys-test/

#for input in ${inputs[@]}
for full_input in inou/yosys/tests/*.v
do
  input=$(basename ${full_input})
  echo ${YOSYS} ./inou/yosys/tests/${input}
  ${YOSYS} ./inou/yosys/tests/${input} > ./yosys-test/log_from_yosys_${input} 2> ./yosys-test/err_from_yosys_${input}

  if [ $? -eq 0 ]; then
    echo "Successfully created graph from ${input}"
  else
    echo "${YOSYS} ./inou/yosys/tests/${input} -d"
    echo "FAIL: lgyosys parsing terminated with an error (testcase ${input})"
    exit 1
  fi

  base=${input%.*}

  #./inou/json/lgjson  --graph_name ${base} --json_output ${base}.json > ./yosys-test/log_json_${input} 2> ./yosys-test/err_json_${input}
  #if [ $? -ne 0 ]; then
    #echo "WARN: Not able to create JSON for testcase ${input}"
  #fi

  ${YOSYS} -g${base} -h > ./yosys-test/log_to_yosys_${input} 2> ./yosys-test/err_to_yosys_${input}

  if [ $? -eq 0 ]; then
    echo "Successfully created verilog from graph ${input}"
  else
    echo ${YOSYS} -g${base} -h -d
    echo "FAIL: verilog generation terminated with an error (testcase ${input})"
    exit 1
  fi

  if [[ $base == "nocheck_*" ]]; then
    echo "Skipping check for "$base
  else
    ${LGCHECK} --implementation=${base}.v --reference=./inou/yosys/tests/${base}.v
    if [ $? -eq 0 ]; then
      echo "Successfully matched generated verilog with original verilog (${input})"
    else
      echo "FAIL: circuits are not equivalent (${input})"
      exit 1
    fi
  fi

done

echo "SUCCESS: all yosys test cases ended without errors"

