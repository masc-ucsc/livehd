#!/bin/bash

declare -a inputs=("trivial.v" "null_port.v" "simple_flop.v" "test.v" "shift.v"\
                   "simple_add.v" \
                   "wires.v" "reduce.v" "graphtest.v" "add.v"  "assigns.v" \
                   "submodule.v" "multiport.v"\
                   "gcd.v" "common_sub.v" "trivial2.v" "consts.v" "async.v"\
                   "unconnected.v" "gates.v" "operators.v" \
                   #"shiftx.v" "regfile2r1w.v" \  #cases currently not working
                   "offset.v" "submodule_offset.v" "mem.v" "mem2.v" "mem_offset.v" \
                   "params.v" "params_submodule.v" "iwls_adder.v")

TEMP=$(getopt -o p --long profile -n 'yosys.sh' -- "$@")
eval set -- "$TEMP"

YOSYS=./inou/yosys/lgyosys
while true ; do
    case "$1" in
        -p|--profile)
          shift
          YOSYS=./inou/yosys/lgyosys --profile
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

./subs/yosys/bin/yosys -V

for input in ${inputs[@]}
do
  ${YOSYS} ./inou/yosys/tests/${input} > ./yosys-test/log_from_yosys_${input} 2> ./yosys-test/err_from_yosys_${input}

  if [ $? -eq 0 ]; then
    echo "Successfully created graph from ${input}"
  else
    echo "${YOSYS} ./inou/yosys/tests/${input} -d"
    echo "FAIL: lgyosys parsing terminated with an error (testcase ${input})"
    exit 1
  fi

  base=${input%.*}

  ./inou/json/lgjson  --graph_name ${base} --json_output ${base}.json > ./yosys-test/log_json_${input} 2> ./yosys-test/err_json_${input}
  if [ $? -ne 0 ]; then
    echo "WARN: Not able to create JSON for testcase ${input}"
  fi

  ${YOSYS} -g${base} -h > ./yosys-test/log_to_yosys_${input} 2> ./yosys-test/err_to_yosys_${input}

  if [ $? -eq 0 ]; then
    echo "Successfully created verilog from graph ${input}"
  else
    echo ${YOSYS} -g${base} -h -d
    echo "FAIL: verilog generation terminated with an error (testcase ${input})"
    exit 1
  fi

  yosys_read="read_verilog -sv ${base}.v; flatten; design -stash gold; read_verilog -sv ./inou/yosys/tests/${base}.v; flatten; design -stash gate; design -copy-from gold -as gold ${base}; design -copy-from gate -as gate ${base}"

  yosys_prep="flatten; proc; memory -nomap;  equiv_make gold gate equiv; prep -flatten -top equiv; hierarchy -top equiv; hierarchy -check; flatten; proc; opt_clean;"

  yosys_equiv="equiv_simple;"
  yosys_equiv_extra="${yosys_simple}; equiv_simple -seq 5; equiv_induct -seq 5;"

  #try fast script first, if it fails, goes to more complex one
  ./subs/yosys/bin/yosys -p "${yosys_read}; ${yosys_prep}; ${yosys_equiv}; equiv_status -assert" \
    2> /dev/null | grep "Equivalence successfully proven!"

  if [ $? -eq 0 ]; then
    echo "Successfully matched generated verilog with original verilog1 (${input})"
  else
    ./subs/yosys/bin/yosys -p "${yosys_read};
    memory -nomap; opt_expr -full; opt -purge; proc; opt -purge;
    opt_reduce -full; opt_expr -mux_undef; opt_reduce; opt_merge; opt_clean -purge;
    ${yosys_prep}; opt -purge; proc; opt -purge; ${yosys_equiv}; equiv_status -assert" \
      | grep "Equivalence successfully proven!"

    if [ $? -eq 0 ]; then
      echo "Successfully matched generated verilog with original verilog2 (${input})"
    else

      ./subs/yosys/bin/yosys -p "${yosys_read};
      memory -nomap; opt_expr -full; opt -purge; proc; opt -purge;
      opt_reduce -full; opt_expr -mux_undef; opt_reduce; opt_merge; opt_clean -purge; techmap -map +/adff2dff.v;
      ${yosys_prep}; opt -purge; proc; opt -purge; ${yosys_equiv_extra}; equiv_status -assert" \
        | grep "Equivalence successfully proven!"

      if [ $? -eq 0 ]; then
        echo "Successfully matched generated verilog with original verilog3 (${input})"


      else
      echo "WARN: Yosys failed to prove equivalency, trying to prove equivalency with Formality."
      echo "./subs/yosys/bin/yosys -p \"${yosys_read}; memory -nomap; opt_expr -full; opt -purge; proc; opt -purge; opt_reduce -full; opt_expr -mux_undef; opt_reduce; opt_merge; opt_clean -purge; techmap -map +/adff2dff.v; ${yosys_prep}; opt -purge; proc; opt -purge; ${yosys_equiv_extra}; equiv_status -assert\""

      echo "
      set_app_var synopsys_auto_setup true
      set_app_var hdlin_ignore_parallel_case false
      set_app_var hdlin_ignore_full_case false
      read_sverilog -r  \"inou/yosys/tests/${base}.v\"
      set_top r:/WORK/${base}
      read_sverilog -i  \"${base}.v\"
      set_top i:/WORK/lgraph_${base}
      match
      report_unmatched_points >> \"fm_${base}_error.log\"
      if { ![verify] }  {
        report_failing_points >> \"fm_${base}_error.log\"
        report_aborted >> \"fm_${base}_error.log\"
        analyze_points -all >> \"fm_${base}_error.log\"
      }
      exit" > fm_script_${base}.tcl

      fm_shell -64bit -f fm_script_${base}.tcl | grep "Verification SUCCEEDED"

      if [ $? -eq 0 ]; then
        echo "Successfully matched generated verilog with original verilog (${input})"
      else
        echo "FAIL: circuits are not equivalent (${input})"
        exit 1
      fi
    fi
  fi
fi

done

echo "SUCCESS: all yosys test cases ended without errors"

