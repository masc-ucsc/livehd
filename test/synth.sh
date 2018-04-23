
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

TEMP=`getopt -o ps:: --long profile,source:: -n 'yosys.sh' -- "$@"`
eval set -- "$TEMP"

YOSYS=./inou/yosys/lgyosys
OPT_LGRAPH=""
while true ; do
    case "$1" in
        -p|--profile)
          shift
          YOSYS=./inou/yosys/lgyosys --profile
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

if [ -z "${OPT_LGRAPH}" ] ; then
  echo "ERROR: -s|--source required and needs to point to lgraph root"
  exit 1
fi

if [ ! -d "$OPT_LGRAPH" ]; then
  echo "Lgraph not found on ${OPT_LGRAPH}"
  exit 1
fi

if [ ! -f "${OPT_LGRAPH}/inou/tech/verilog.rb" ]; then
  echo "verilog.rb not found on ${OPT_LGRAPH}"
  exit 1
fi


rm -rf ./lgdb/ ./logs ./synth-test *.yaml *.v
mkdir synth-test/
mkdir logs
mkdir lgdb

ruby ${OPT_LGRAPH}/inou/tech/verilog_json.rb \
  ${OPT_LGRAPH}/misc/yosys/techlibs/common/simcells.v  \
  ${OPT_LGRAPH}/misc/yosys/techlibs/xilinx/cells_sim.v \
  ${OPT_LGRAPH}/misc/yosys/techlibs/xilinx/brams_bb.v \
  ${OPT_LGRAPH}/misc/yosys/techlibs/xilinx/drams_bb.v  > lgdb/tech_library

for input in ${inputs[@]}
do

  synth_script="hierarchy -auto-top; clean; proc_arst; proc; opt -fast; pmuxtree; memory -nomap;
  hierarchy -check -auto-top; proc; flatten; synth -run coarse;
  memory_bram -rules +/xilinx/brams.txt; techmap -map +/xilinx/brams_map.v; memory_bram -rules +/xilinx/drams.txt; techmap -map +/xilinx/drams_map.v;
  opt -fast -full; memory_map; dffsr2dff; dff2dffe; opt -full;
  techmap -map +/techmap.v -map +/xilinx/arith_map.v; opt -fast; techmap -D ALU_RIPPLE;
  opt -fast; abc -D 100 cpu_bug;"
  ./misc/yosys/bin/yosys -m ./inou/yosys/libinou_yosys.so -p "read_verilog -sv ./inou/yosys/tests/${input};
   ${synth}; write_verilog ${input}_synth.v; inou_yosys lgdb" > ./synth-test/log_from_yosys_${input} 2> ./synth-test/err_from_yosys_${input}


  #${YOSYS} ./inou/yosys/tests/${input} > ./synth-test/log_from_yosys_${input} 2> ./synth-test/err_from_yosys_${input}

  if [ $? -eq 0 ]; then
    echo "Successfully created graph from "${input}
  else
  echo "gdb --args ./misc/yosys/bin/yosys -m ./inou/yosys/libinou_yosys.so -p \"read_verilog -sv ./inou/yosys/tests/${input};
    ${synth};  write_verilog ${input}_synth.v; inou_yosys lgdb\""
    echo "FAIL: lgyosys parsing terminated with an error (testcase ${input})"
    exit 1
  fi

  base=${input%.*}
  ./inou/yaml/lgyaml  --graph_name ${base} --yaml_output ${base}.yaml > ./synth-test/log_yaml_${input} 2> ./synth-test/err_yaml_${input}
  if [ $? -ne 0 ]; then
    echo "WARN: Not able to create YAML for testcase ${input}"
  fi

  ./inou/json/lgjson  --graph_name ${base} --json_output ${base}.json > ./synth-test/log_json_${input} 2> ./synth-test/err_json_${input}
  if [ $? -ne 0 ]; then
    echo "WARN: Not able to create json for testcase ${input}"
  fi

  ${YOSYS} -g${base} -h > ./synth-test/log_to_yosys_${input} 2> ./synth-test/err_to_yosys_${input}

  if [ $? -eq 0 ]; then
    echo "Successfully created verilog from graph "${input}
  else
    echo "${YOSYS} -g${base} -h -d"
    echo "FAIL: verilog generation terminated with an error (testcase ${input})"
    exit 1
  fi

  yosys_read="read_verilog -sv ${base}.v; flatten; design -stash gold;
  read_verilog -sv ./inou/yosys/tests/${base}.v; flatten; design -stash gate;
  design -copy-from gold -as gold ${base}; design -copy-from gate -as gate ${base}"

  yosys_prep="flatten; proc; memory -nomap;
  equiv_make gold gate equiv;
  prep -flatten -top equiv;
  hierarchy -top equiv; hierarchy -check; flatten; proc; opt_clean;"

  yosys_equiv="equiv_simple;"
  yosys_equiv_extra="${yosys_simple}; equiv_simple -seq 5; equiv_induct -seq 5;"

  #try fast script first, if it fails, goes to more complex one
  ./misc/yosys/bin/yosys -p "${yosys_read}; ${yosys_prep}; ${yosys_equiv}; equiv_status -assert" \
    2> /dev/null | grep "Equivalence successfully proven!"

  if [ $? -eq 0 ]; then
    echo "Successfully matched generated verilog with original verilog1 ("${input}")"
  else

    ./misc/yosys/bin/yosys -p "${yosys_read}; memory -nomap; opt_expr -full; opt -purge; proc; opt -purge;
    opt_reduce -full; opt_expr -mux_undef; opt_reduce; opt_merge; opt_clean -purge; ${yosys_prep}; opt -purge; proc; opt -purge; ${yosys_equiv}; equiv_status -assert" \
      | grep "Equivalence successfully proven!"

    if [ $? -eq 0 ]; then
      echo "Successfully matched generated verilog with original verilog2 ("${input}")"
    else

      if [ $? -eq 0 ]; then
        echo "Successfully matched generated verilog with original verilog2 ("${input}")"
      else

        ./misc/yosys/bin/yosys -p "${yosys_read}; memory -nomap; opt_expr -full; opt -purge; proc; opt -purge;
        opt_reduce -full; opt_expr -mux_undef; opt_reduce; opt_merge; opt_clean -purge; techmap -map +/adff2dff.v;
        ${yosys_prep}; opt -purge; proc; opt -purge; ${yosys_equiv_extra}; equiv_status -assert" \
          | grep "Equivalence successfully proven!"

        if [ $? -eq 0 ]; then
          echo "Successfully matched generated verilog with original verilog3 ("${input}")"

        else
          echo "WARN: Yosys failed to prove equivalency, trying to prove equivalency with Formality."
          echo "./misc/yosys/bin/yosys -p \"read_verilog -sv ${base}.v; read_verilog -sv ./inou/yosys/tests/${base}.v; flatten; proc; memory -nomap; opt_expr -full; opt -purge; proc; opt -purge; opt_reduce -full; opt_expr -mux_undef; opt_reduce; opt_merge; opt_clean -purge; equiv_make ${base} lgraph_${base} equiv; hierarchy -top equiv; hierarchy -check; flatten; opt -purge; proc; opt -purge; equiv_simple; equiv_status -assert\""

          echo "source /mada/software/synopsys/F-2011.09-SP1formality/admin/setup/.synopsys_fm.setup
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
            echo "Successfully matched generated verilog with original verilog ("${input}")"
          else
            echo "FAIL: circuits are not equivalent ("${input}")"
            exit 1
          fi
        fi
      fi
    fi
  fi
done

echo "SUCCESS: all yosys test cases ended without errors"

