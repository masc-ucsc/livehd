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



LIBERTY_FILE="/home/sgarg3/livehd/sky130_fd_sc_hd__ff_100C_1v95.lib"
test_dir=$(pwd)
rtl_real_src_dir="${test_dir}/rtl_real_source"
synth_dir="${test_dir}/nl_single"
synalign_log_file="${test_dir}/extractor_alignment_tests/${top_name}_Synalign.log"

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

CXX=clang++-14 CC=clang-14 bazel build -c opt //...
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
    echo "$synth_dir already exists. Skipping synthesis using yosys!
    If you want to re-run synthesis, delete the directory and retry."
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

# Output the slack value
echo "arrival_time: $arrival_time"


echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
echo "          creating color.json for $top_name ...       "
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"

python3 ../create_color_json.py $latest_report $top_name
if [ $? -ne 0 ]; then
	echo " ERROR: error in ../create_color_json.py"
	exit 1
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
	
	CXX=clang++-14 CC=clang-14 bazel build -c opt //...
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
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
echo "          commenting rtl using $synalign_log_file ...       "
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
 python3 ../comment_rtl.py ${synalign_log_file}
if [ $? -ne 0 ]; then
	echo " ERROR: error in ../comment_rtl.py"
	exit 1
fi
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
echo "          commented rtl using $synalign_log_file ...       "
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"

}


lgcheck(){

# Move to livehd directory
cd ~/livehd/
ret_val=$?
if [ $ret_val -ne 0 ]; then
  echo "\n--------livehd folder not found. check the directory structure and make necessary changes.--------\n\n"
  exit $ret_val
fi


inou/yosys/lgcheck --top

}

create_yaml_for_hagent() {
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
	    python3 ../create_yaml_for_hagent.py $file -m "openai/o3-mini-2025-01-31" -o ${rtl_path}/liveparse/$output_file
	done



}


##call_hagent() {
##}

##./clean_tests.sh
##create_selected_top_file
##split_into_modules
##synth_yosys
##calc_frequency_and_create_color_dot_json
###now we need to lower the arrival time to improve frequency.
###Which part of rtl does the nodes in above reported timing path belong to?
##run_synalign  
###orig files are from liveparse/
##cd $test_dir
##comment_rtl
##
##echo `pwd`

#now use AI to generate optimized versions of _commented.v files(modules)
create_yaml_for_hagent
##call_hagent to create _optimized.v as well as run lec check on _optimized and _commented versions


#if lec fails, get another version from LLM and retry LEC until it passes.
##write the chat command: "your solution fails LEC. try again."

#if lec passes, stitch optimized version with all other modules and create a neww top and re run synth+timing to check if freq. improved!

