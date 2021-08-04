#!/bin/bash

pts_to_be_merged='io_gen io_gen2 io_gen3 test2'
pts_tuple_dbg='lhs_wire3 funcall_unnamed2
               firrtl_gcd counter_tup counter2'

pts_long_time='firrtl_gcd'

# pts_tbd='tup_out1 tup_out2'
pts_after_micro='hier_tuple4 tuple_reg3'

# **WARNING** Use the pass/compiler/BUILD to add the tests. Anything here would
# NOT affect the regression. It is more for "work-in-progress" tests.

pts_pass='scalar_tuple flatten_bundle partial hier_tuple reg_bits_set bits_rhs reg__q_pin
hier_tuple_io hier_tuple3 tuple_if ssa_rhs out_ssa attr_set lhs_wire tuple_copy
if1 lhs_wire2 tuple_copy2 counter adder_stage logic2 tuple_empty_attr if2
scalar_reg_out_pre_declare firrtl_tail2 hier_tuple_nested_if3
hier_tuple_nested_if5 hier_tuple_nested_if6 hier_tuple_nested_if7 firrtl_tail
nested_if counter_nested_if tuple_reg tuple_reg2 tuple_nested1 tuple_nested2
get_mask1 vec_shift_register_param hier_tuple2 capricious_bits
capricious_bits2 capricious_bits4 hier_tuple_nested_if hier_tuple_nested_if2
struct_flop hier_tuple_nested_if4 firrtl_gcd_3bits firrtl_tail3
hier_tuple2 capricious_bits capricious_bits2 capricious_bits4'

pts='hier_tuple'

if [ $# -eq 1 ]; then
  pts=$(basename $1)
  pts=${pts%.prp}
	PATTERN_PATH=$(dirname $1)
elif [ $# -ne 0 ]; then
	echo "SPECIFY one test at a time"
	exit 3
else
	PATTERN_PATH=./inou/pyrope/tests
fi

# FIXME: extra flop left around!! (the test fails because this extra flop has no name and cgen creates incorrect verilog)
# pts='counter_mix'  
# pts='memory_1rd1wr'
# pts='masked_smem'
# pts='pp'
# pts='memory_1rd1wr'
# pts='pp2'
# pts='vector'
# pts='vector2'
# pts='hier_tuple_nested_if8'  # LNAST_TO failure
# pts='tuple_if2'

# Note: in this bash script, you MUST specify top module name AT FIRST POSITION
#pts_hier='top'

LGSHELL=./bazel-bin/main/lgshell
LGCHECK=./inou/yosys/lgcheck

if [ ! -f $LGSHELL ]; then
    if [ -f ./main/lgshell ]; then
        LGSHELL=./main/lgshell
        echo "lgshell is in $(pwd)"
    else
        echo "ERROR: could not find lgshell binary in $(pwd)";
    fi
fi

Pyrope_step () {

  top_module=$1
	shift 1

	all_files=$@
	gold_verilog="${PATTERN_PATH}/verilog_gld/${top_module}.gld.v"

	for f in $(echo ${all_files} | sed -es/,/\ /g)
	do
		if [ ! -f "${f}" ]; then
			echo "Could not find input pyrope ${f} testing"
			exit 3
		fi
	done

  # if [ ! -f "${gold_verilog}" ]; then
		# echo "Could not find reference verilog for testing: ${gold_verilog}"
		# exit 3
	# fi

	rm -rf lgdb_prp
  ${LGSHELL} "inou.pyrope files:${all_files} |> pass.lnast_tolg.dbg_lnast_ssa |> lnast.dump " > ${pt}.lnast.txt
	${LGSHELL} "inou.pyrope files:${all_files} |> pass.compiler path:lgdb_prp gviz:true top:${top_module}"
	ret_val=$?
	if [ $ret_val -ne 0 ]; then
		echo "ERROR: could not direct compile with files:${all_files}!"
		exit $ret_val
	fi

	rm -rf tmp_prp_v
	${LGSHELL} "lgraph.open hier:true path:lgdb_prp name:${top_module} |> inou.cgen.verilog odir:tmp_prp_v"
	if [ $? -eq 0 ] && [ -f "tmp_prp_v/${pt}.v" ]; then
		echo "Successfully generate Verilog: tmp_prp_v/${pt}.v"
	else
		echo "ERROR: Pyrope compiler failed: direct verilog generation, testcase: ${all_files2}"
		exit 1
	fi
	cat tmp_prp_v/*.v >tmp_prp_v/all_${top_module}.v

	# WARNING: Verilog can have backslash in name. So get it from the generated verilog
	top_verilog_name=$(grep module tmp_prp_v/${top_module}.v | grep -v endmodule | cut -d" " -f2 | sed -es/\(//g)
	if [ ${top_verilog_name} != ${top_module} ]; then
		echo "top verilog ${top_verilog_name} and top module name ${top_module} do not match (reserved verilog keyword?)"
		exit 4
	fi

  ${LGCHECK} --top $top_verilog_name --implementation tmp_prp_v/all_${top_module}.v --reference ${PATTERN_PATH}/verilog_gld/${top_module}.gld.v
  if [ $? -eq 0 ]; then
		echo "Successfully pass logic equivilence check!"
  else
		echo "FAIL: direct tmp_prp_v/all_${top_module}.v !== ${top_module}.gld.v (top verilog name is ${top_verilog_name})"
		exit 1
  fi

	########################################
	# Trying the same code but using the PRP2PRP pass

	rm -rf tmp_prp
	${LGSHELL} "inou.pyrope files:${all_files} |> inou.code_gen.prp odir:tmp_prp"
	ret_val=$?
	if [ $ret_val -ne 0 ]; then
		echo "ERROR: could not prp2prp top: ${top_module} with files:${all_files}!"
		exit $ret_val
	fi

	rm -rf lgdb_prp2prp
	${LGSHELL} "files path:tmp_prp match:\".*\.prp\" |> inou.pyrope |> pass.compiler path:lgdb_prp2prp gviz:true top:${top_module}"
	ret_val=$?
	if [ $ret_val -ne 0 ]; then
		echo "ERROR: could not prp2prp compile with files:${all_files}!"
		exit $ret_val
	fi

	rm -rf tmp_prp2prp_v
	${LGSHELL} "lgraph.open hier:true path:lgdb_prp2prp name:${top_module} |> inou.cgen.verilog odir:tmp_prp2prp_v"
	if [ $? -eq 0 ] && [ -f "tmp_prp2prp_v/${pt}.v" ]; then
		echo "Successfully generate Verilog: tmp_prp2prp_v/${pt}.v"
	else
		echo "ERROR: Pyrope compiler failed: prp2prp verilog generation, testcase: ${all_files}"
		exit 1
	fi
	cat tmp_prp2prp_v/*.v >tmp_prp2prp_v/all_${top_module}.v

  ${LGCHECK} --top $top_module --implementation tmp_prp2prp_v/all_${top_module}.v --reference ${PATTERN_PATH}/verilog_gld/${top_module}.gld.v
  if [ $? -eq 0 ]; then
      echo "Successfully pass logic equivilence check!"
  else
      echo "FAIL: prp2prp tmp_prp2prp_v/all_${top_module}.v !== ${top_module}.gld.v"
      exit 1
  fi
}

Pyrope_compile() {
  echo "===================================================="
  echo "Hierarchical Pyrope Full Compilation to check high level LN generation (code_gen)"
  echo "===================================================="

  top_module=$1
	top_file=${PATTERN_PATH}/${top_module}.prp

	if [ ! -f ${top_file} ]; then
		echo "ERROR: could not find ${top_file} in ${PATTERN_PATH}"
		exit 1
	fi

	# Get all the files associated with this separated with commas
	xtra_files=$(ls -m ${PATTERN_PATH}/${top_module}_*.prp)

	if [ ! -z "${xtra_files}" ]; then
		all_files="${top_file}, "$(ls -m ${PATTERN_PATH}/${top_module}_*.prp)
		Pyrope_step $top_module ${all_files}

		all_files2=$(ls -m ${PATTERN_PATH}/${top_module}_*.prp)", ${top_file}"
		Pyrope_step $top_module ${all_files}
	else
		Pyrope_step $top_module ${PATTERN_PATH}/${top_module}.prp
	fi
}

for pt in $pts_hier
do
	rm -rf ./lgdb
	Pyrope_compile "$pt"
done

for pt in $pts
do
	rm -rf ./lgdb
	Pyrope_compile "$pt"
done

