#!/bin/bash


# HOW TO USE:
# ../splitRTLandSynthesize.sh
#
# REQUIRED FOLDER: rtl_real_source/<top file>.v
# This file takes a top module (with sub modules in the same file) file
# and split all the modules in different files 
# these files are in rtl_modules/ folder 
# Run this script in test<testname> folder and you will get the rtl_modules/ there itself
# this script uses inou.liveparse from livehd

declare -a results_array #declaring array called results_array to capture the arrival time imrpovements in result_file 
declare -a file_names_to_change # Declare an array to store file names

LIBERTY_FILE="/home/sgarg3/livehd/sky130_fd_sc_hd__ff_100C_1v95.lib"
test_dir=$(pwd)
rtl_real_src_dir="${test_dir}/rtl_real_source"
synth_dir="${test_dir}/nl_single"
synalign_log_file="${test_dir}/extractor_alignment_tests/${top_name}_Synalign.log"
original_arrival_time=0.00
# which is the top module?
if [[ "$PWD" == *"PipelineDino"* ]]; then
    top_name="PipelinedCPU"
    original_top_file=$rtl_real_src_dir/Top.v
    synth_file="$synth_dir/${top_name}_synth.v"
else
    echo "ERROR: not in any known directory. Exiting..."
    exit 1
fi

rtl_path="${test_dir}/rtl_modules"
rtl_selected_top_path="${test_dir}/rtl_single"
rtl_selected_top_file="${rtl_selected_top_path}/${top_name}.v"
rtl_opt_dir="${test_dir}/rtl_optimized"
INPUT_YAMLS="/home/sgarg3/hagent/hagent/step/replicate_code/tests/input_yamls"
GEN_YAMLS="/home/sgarg3/hagent/generated_yamls"

#example of what this function does: top.v(rtl)--{yosys}-->rtl_single/pipelinedCPU.v(rtl)
create_selected_top_file () {
echo ""
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
echo "       Processing files: $rtl_real_src_dir/* --> $rtl_selected_top_file           "
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
echo "" 

# Check if rtl/ directory exists and is not empty
if [ ! -d $rtl_real_src_dir ] || [ -z "$(ls -A $rtl_real_src_dir)" ]; then
    echo "Error: rtl_real_source/ directory is missing or empty. 
          Are you in the directory where you have the rtl_real_source/ folder?
	  example: ~/livehd/pass/extractor/tests/testAdSubComb will have rtl_real_source/ and 
	  this script will be run in testAdSubComb/ directory
	  output: synthesized netlist will be nl_single/<rtl_file_name>_synth.v"
    exit 1
fi

mkdir -p ${rtl_selected_top_path}

#create new rtl file for the top module we want to synthesize. Example PipelinedCPU shall be the top instead of "Top".
yosys -p "
    read_verilog -sv -defer $rtl_real_src_dir/*;
    hierarchy -top $top_name;
    write_verilog ${rtl_selected_top_file};
"
ret_val=$?
if [ $ret_val -ne 0 ]; then
  echo "\n--------Could not create rtl_single?.--------\n\n"
  exit $ret_val
fi

}


#example of what this function does: pipelinedCPU.v(rtl)---{liveHD}--->rtl_modules/ control.v, alu.v, PipelinedCPU.v, etc. (rtl)
split_into_modules () {

echo ""
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
echo "       Processing file: $rtl_selected_top_file --> rtl_modules/liveparse/          "
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
echo "" 

# Create rtl_modules/ directory in current directory
if [ -d $rtl_path ]; then
	echo "WARNING: rtl_modules folder already exists. 
	deleting rtl_modules folder..."
	rm -r ${rtl_path}
fi
mkdir ${rtl_path}
echo "Created RTL directory at: $rtl_path"

# Move to livehd directory
cd ~/livehd/
ret_val=$?
if [ $ret_val -ne 0 ]; then
  echo "\n--------livehd folder not found. check the directory structure and make necessary changes.--------\n\n"
  exit $ret_val
fi

bazel build -c opt //...
ret_val=$?
if [ $ret_val -ne 0 ]; then
  echo "\n--------compilation failed!--------\n\n"
  exit $ret_val
fi
if [ -d lgdb/ ]; then
	rm -r lgdb/
fi

./bazel-bin/main/lgshell "inou.liveparse files:${rtl_selected_top_file} path:${rtl_path}"

cd $test_dir ;


}


#example of what this function does: pipelinedCPU.v(rtl)---{yosys}--->nl_single/PipelinedCPU_synth.v (netlist)
synth_yosys () {
###########Synthesize top:

echo ""
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
echo "          Synthesizing TOP using Yosys ...          "
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
echo "" 



# Remove existing nl_single/ directory if it exists
if [ -d $synth_dir ]; then
    echo "$synth_dir already exists. removing it..."
    rm -r $synth_dir/*
else
    mkdir $synth_dir
    echo "Created new $synth_dir directory."
    
fi

echo "Processing $rtl_selected_top_file (Module: $top_name) -> $synth_file"

# Run Yosys synthesis
yosys -p "
    read_verilog -sv -defer $rtl_selected_top_file;
    hierarchy -top $top_name;
    flatten $top_name;
    opt; 
    synth -top $top_name;
    dfflibmap -liberty $LIBERTY_FILE;
    printattrs;
    stat;
    abc -liberty $LIBERTY_FILE  -dff -keepff -g aig;
    stat;
    write_verilog $synth_file;
" > synth_top.log
if [ $? -eq 0 ]; then
    echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
    echo "          Synthesis using Yosys completed.          "
    echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
else
    echo "~~~~~~~~~~~~~ ERROR: YOSYS synthesis failed!  ~~~~~~~~~~~"
    exit 1
fi
}

#example of what this function does: nl_single/PipelinedCPU_synth.v (netlist) --openSTA---> timing_report*rpt formed with max delay path -----> color.json
calc_frequency_and_create_color_dot_json () {
	run_with_synalign=$1
##### STA on top module
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
echo "          Find frequency using openSTA for $top_name ...       "
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"

# Run OpenSTA
echo "source run_sta.tcl" | ~/opensta/OpenSTA/app/sta

# Find the latest timing report file
latest_report=$(ls -v timing_report*rpt 2>/dev/null | tail -n 1)

# Check if a file was found
if [ -n "$latest_report" ]; then
    echo "Reading latest timing report: $latest_report"

    cat "$latest_report"
else
    echo "No timing report found."
    exit 1
fi

# Extract the line with "slack" and get the corresponding value
# -m 1 --> will select only the 1st occurence. otherwise all the slack values will be printed.
arrival_time=$(grep -m 1 -oP '\s*-?\d+\.\d+\s+data arrival time' "$latest_report" | awk '{print $1}')
original_arrival_time=$arrival_time
# Output the slack value
echo "arrival_time: $arrival_time"

if [ "$run_with_synalign" = "true" ]; then
	echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
	echo "          creating color.json for $top_name ...       "
	echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
	
	python3 ../create_color_json.py $latest_report $top_name
	if [ $? -ne 0 ]; then
		echo " ERROR: error in ../create_color_json.py"
		exit 1
	fi
fi

}

run_synalign () {
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
echo "          Running SynAlign ...       "
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
	
	if [ ! -d extractor_alignment_tests/ ]; then
	  mkdir -p ${test_dir}/extractor_alignment_tests/
	fi
	
	
	# Move to livehd directory
	cd ~/livehd/
	ret_val=$?
	if [ $ret_val -ne 0 ]; then
	  echo "\n--------livehd folder not found. check the directory structure and make necessary changes.--------\n\n"
	  exit $ret_val
	fi
	
	bazel build -c opt //...
	ret_val=$?
	if [ $ret_val -ne 0 ]; then
	  echo "\n--------compilation failed!--------\n\n"
	  exit $ret_val
	fi
	
	if [ -d lgdb/ ]; then
		rm -r lgdb/
	fi
	./bazel-bin/main/lgshell "inou.liberty files:sky130_fd_sc_hd__ff_100C_1v95.lib"
	
	#REMEBER TO RENAME THE FILES
	# Use sed to replace "module <top_name>" with "module <top_name>_original" in place
	#sed -i "s/module[[:space:]]\+$top_name\>/module ${top_name}_original/" "$rtl_selected_top_file" --> not using this because this will lead to top.v in loc info
	sed -i "s/module[[:space:]]\+$top_name\>/module ${top_name}_original/" "$rtl_path/liveparse/$top_name.v"

	echo "NOTE: Updated module name in $rtl_path/liveparse/$top_name.v"
	
	# Get all .v files in rtl_path and join them with commas
	file_list=$(find "$rtl_path/liveparse/" -maxdepth 1 -type f -name "*.v" | paste -sd "," -)
	# Check if file_list is empty
	if [ -z "$file_list" ]; then
	    echo "No .v files found in $rtl_path"
	    exit 1
	fi

	./bazel-bin/main/lgshell " 
	inou.yosys.tolg files:${synth_file} |> inou.attr.load files:${test_dir}/color.json
	inou.yosys.tolg top:${top_name}_original files:${file_list}
	lgraph.open name:${top_name}_original |> lgraph.open name:${top_name}|> lgraph.dump hier:true |> inou.traverse_lg LGorig:${top_name}_original LGsynth:${top_name}
	" > ${synalign_log_file}
	ret_val=$?
	if [ $ret_val -ne 0 ]; then
	  echo "\n--------${top_name} failed!--------\n\n"
	  exit $ret_val
	else
	  echo "$top_name done!"
	fi
}


comment_rtl() {
	run_with_synalign=$1
	echo "~~~~~~ in comment_rtl function with synalign run as $run_with_synalign"
if [ "$run_with_synalign" == "true" ]; then
	echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
	echo "          commenting rtl using $synalign_log_file ...       "
	echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"

	# Capture JSON output and parse it
        #file_names_to_change=($(python3 ../comment_rtl.py "${synalign_log_file}" | jq -r '.[]'))
	mapfile -t file_names_to_change < <(python3 ../comment_rtl.py "${synalign_log_file}" | jq -r '.[]')
	#python3 ../comment_rtl.py ${synalign_log_file}
	
	echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
	echo "          commented ${file_names_to_change[@]} rtl        "
	echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
else
	echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
	echo "creating commented files (echo "${file_names_to_change[@]}" ) ...       "
	echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"

	# Traverse over the list and rename files (need not put //CRITICAL in them)
	for file in "${file_names_to_change[@]}"; do
	    dir_name=$(dirname "$file")  # Extract directory path
	    base_name=$(basename "$file" .v)  # Extract filename without extension
	    new_name="${dir_name}/${base_name}_commented.v"  # Append _commented
	
	    # Rename the file
	    cp "$file" "$new_name"
	
	    # Print status
	    echo "copied: $file → $new_name"
	done
fi

if [ $? -ne 0 ]; then
	echo " ERROR: error in ../comment_rtl.py"
	exit 1
fi
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
echo "          commented rtl using $synalign_log_file ...       "
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"

}


create_yaml_and_call_hagent() {

	run_with_synalign=$1

	if [ "$run_with_synalign" = "true" ]; then
		# Use sed to replace "module <top_name>_original" with "module <top_name>" in place
		sed -i "s/module[[:space:]]\+${top_name}_original\>/module ${top_name}/" "$rtl_path/liveparse/${top_name}.v"
		if [ -f "$rtl_path/liveparse/${top_name}_commented.v" ]; then
			sed -i "s/module[[:space:]]\+${top_name}_original\>/module ${top_name}/" "$rtl_path/liveparse/${top_name}_commented.v"
		fi
		echo "NOTE: Reverted SynAlign renaming in $rtl_path/liveparse/${top_name}.v"
	fi


	# Find all files ending with "_commented.v" and store in an array
	files=($(find "${rtl_path}/liveparse/" -type f -name "*_commented.v"))

	# Check if any files were found
	if [ ${#files[@]} -eq 0 ]; then
	    echo "No _commented.v files found in $rtl_path/liveparse"
	    exit 1
	fi

	# Print the list of files
	echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
	echo "yaml to be made for:"
	for file in "${files[@]}"; do
	    echo "$file"
	done
	echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"

	# Iterate over each file and run generate_yaml.py
	for file in "${files[@]}"; do
	    base_name=$(basename "$file" "_commented.v")  # Extract module name
	    output_file="${base_name}.yaml"  # Construct output file name
	    echo "Creating yaml for $file -> $output_file"
	    python3 ../create_yaml_for_hagent.py $file $base_name -c $run_with_synalign -m "openai/o3-mini-2025-01-31" -o ${rtl_path}/liveparse/$output_file

	    #now call hagent for each of these files.

	    # Check and create input_yamls if it doesn't exist
	    if [ ! -d "$INPUT_YAMLS" ]; then
	            echo "Creating directory: $INPUT_YAMLS"
	            mkdir -p "$INPUT_YAMLS"
	    else
	            echo "Directory already exists: $INPUT_YAMLS"
	    fi
	    
	    # Check and create generated_yamls if it doesn't exist
	    if [ ! -d "$GEN_YAMLS" ]; then
	            echo "Creating directory: $GEN_YAMLS"
	            mkdir -p "$GEN_YAMLS"
	    else
	            echo "Directory already exists: $GEN_YAMLS"
	    fi
	    
	    #run hagent:
	    cp ${rtl_path}/liveparse/$output_file ${INPUT_YAMLS}/$output_file 
	    echo "Copying ${rtl_path}/liveparse/$output_file to ${INPUT_YAMLS}/$output_file"


	    #go to hagent replicate_code directory
	    cd ~/hagent/
	    ret_val=$?
	    if [ $ret_val -ne 0 ]; then
	      echo "\n--------hagent folder not found. check the directory structure and make necessary changes.--------\n\n"
	      exit $ret_val
	    fi
	    #rm res.log ; #rm replicate_code.log ; #poetry run python3 ./hagent/step/replicate_code/replicate_code.py -ogenerated_yamls/ALU.yaml ./hagent/step/replicate_code/tests/input_yamls/ALU.yaml |& tee res.log 
	    poetry run python3 ./hagent/step/replicate_code/replicate_code.py -o${GEN_YAMLS}/${base_name}.yaml ${INPUT_YAMLS}/${base_name}.yaml 2>&1 | tee ${GEN_YAMLS}/${base_name}.log
	    echo "writing to ${GEN_YAMLS}/${base_name}.log...."
	    cd $test_dir 

	done


}

synth_yosys_and_calc_new_freq() {

	echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
	echo "          Synthesize optimized design: $top_name ...       "
	echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
	## #Check if any file ending with "_optimized.v" is created using hagent:
	## #if they are created then bring them (without _optimized.v) to rtl_optimized directory in the test dir
	if [ ! -d "$rtl_opt_dir" ]; then
	        mkdir -p "$rtl_opt_dir"
	else
	        rm -r $rtl_opt_dir/*
	fi
	#get all orig files for synth:
	#files=$(find "${rtl_path}/liveparse" -type f -name "*.v" ! -name "*_optimized.v" ! -name "*_commented.v") # Find all .v files but exclude _optimized.v and _commented.v
	find "$rtl_path/liveparse" -type f -name "*.v" ! -name "*_optimized.v" ! -name "*_commented.v" -exec cp {} "$rtl_opt_dir" \;
	#get optimized file from GEN_YAMLS and use these optimized modules instead of original modules:
	files=$(find "$GEN_YAMLS" -type f -name "*_optimized.v")
	if [ -z "$files" ]; then
		echo "No *_optimized.v files found in $GEN_YAMLS."
	else
		echo "$files" | while read -r file; do
			new_name=$(basename "$file" | sed 's/_optimized//')
			cp "$file" "$rtl_opt_dir/$new_name"
			echo "Copied and renamed: $file -> $rtl_opt_dir/$new_name"
		done
	fi

	#now we have all files to be synthesized in rtl_opt_dir
	mkdir $rtl_opt_dir/synth_file
	optimized_netlist="$rtl_opt_dir/synth_file/${top_name}_synth.v"
	yosys -p "
		read_verilog -sv -defer $rtl_opt_dir/*.v
    		hierarchy -top $top_name;
    		flatten $top_name;
    		opt; 
    		synth -top $top_name;
    		dfflibmap -liberty $LIBERTY_FILE;
    		printattrs;
    		stat;
    		abc -liberty $LIBERTY_FILE  -dff -keepff -g aig;
    		stat;
    		write_verilog $optimized_netlist;
	" > synth_top.log
	if [ $? -eq 0 ]; then
	    echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
	    echo "          Synthesis using Yosys completed.          "
	    echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
	else
	    echo "~~~~~~~~~~~~~ ERROR: YOSYS synthesis failed!  ~~~~~~~~~~~"
	    exit 1
	fi


	echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
	echo "          Find frequency using openSTA for $top_name ...       "
	echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
	cd $test_dir
	mkdir $rtl_opt_dir/timing
	# Define variables
	new_sta_tcl="$rtl_opt_dir/timing/run_sta.tcl"  # Replace with the actual SDC filename
	
	# Create the SDC file and write the required content
	cat > "$new_sta_tcl" <<EOF
	read_liberty $LIBERTY_FILE
	set_units -time ns -capacitance pF -voltage V -current mA -resistance kOhm -distance um
	set_operating_conditions ff_100C_1v95
	read_verilog $optimized_netlist
	link_design $top_name
	read_sdc ${top_name}.sdc
	report_checks -path_delay max > ${rtl_opt_dir}/timing/timing_report.rpt
EOF
	
	echo "SDC file created: $new_sta_tcl"
	
	# Run OpenSTA
	echo "source $new_sta_tcl" | ~/opensta/OpenSTA/app/sta
	
	# Find the latest timing report file
	latest_report=$(ls ${rtl_opt_dir}/timing/timing_report.rpt 2>/dev/null | tail -n 1)
	# Check if a file was found
	if [ ! -n "$latest_report" ]; then
	    echo "No timing report found."
	    exit 1
	fi
	
	# Extract the line with "slack" and get the corresponding value
	arrival_time=$(grep -m 1 -oP '\s*-?\d+\.\d+\s+data arrival time' "$latest_report" | awk '{print $1}')
	# Output the slack value
	echo "new arrival_time: $arrival_time , original_arrival_time=$original_arrival_time"
	
	improved_arrival_time_diff=$(echo "$original_arrival_time - $arrival_time" | bc)
	results_array+=($improved_arrival_time_diff)
	echo "####################################################################"
	echo "NOTE: improvement in arrival time: $improved_arrival_time_diff ns"
	echo "####################################################################"


}


run_all(){
	run_with_synalign=$1
	../clean_tests.sh
	create_selected_top_file
	split_into_modules
	synth_yosys
	calc_frequency_and_create_color_dot_json "$run_with_synalign"
	#now we need to lower the arrival time to improve frequency.
	#Which part of rtl does the nodes in above reported timing path belong to?
	if [ "$run_with_synalign" = "true" ]; then
		run_synalign  
	fi
	#orig files are from liveparse/
	cd $test_dir
	comment_rtl "$run_with_synalign"
	echo `pwd`
	
	#now use AI to generate optimized versions of _commented.v files(modules)
	create_yaml_and_call_hagent "$run_with_synalign"
	#call_hagent to create _optimized.v as well as run lec check on _optimized and _commented versions
	#if lec passes, stitch optimized version with all other modules and create a neww top and re run synth+timing to check if freq. improved!
	
	synth_yosys_and_calc_new_freq

}


# File to collect results
result_file="results.txt"
echo "--------------" >> "$result_file"  # separator before new results
echo "$top_name" >> "$result_file"

#always run forst with Synalign so that in next (w/o SynAlign) run, then same files are taken for optimization
for run_with_synalign in true false; do
	results_array=()
	run_all "$run_with_synalign" 
	# if [ "$run_with_synalign" == "true" ]; then
	# 	echo "With SynAlign" >> "$result_file"
	# else
	# 	echo "Without SynAlign" >> "$result_file"
	# fi
	echo "run_with_syn=$run_with_synalign, results_array=$(IFS=,; echo "${results_array[*]}")" >> "$result_file"  # Append to file
done

# Store the results as a comma-separated string
#result_str=$(IFS=,; echo "${results_array[*]}")
#echo "$result_str" >> "$result_file"

# Display the results
cat "$result_file"

