#!/bin/bash


# how to run:
# ../run_yosys_synth.sh
# run this file in the directory where you have the rtl/ folder.
# example: ~/livehd/pass/extractor/tests/testAdSubComb will have rtl/ and 
# this script will be run in testAdSubComb/ directory
# output: all the .v files will be converted to _synth.v



LIBERTY_FILE="/home/sgarg3/livehd/sky130_fd_sc_hd__ff_100C_1v95.lib"


# Check if rtl/ directory exists and is not empty
if [ ! -d "rtl" ] || [ -z "$(ls -A rtl)" ]; then
    echo "Error: rtl/ directory is missing or empty. Check how to run in the script"
    exit 1
fi

# Remove existing synth/ directory if it exists
if [ -d "synth" ]; then
    rm -rf synth
    echo "Removed existing synth/ directory."
fi

# Create new synth/ directory
mkdir synth
echo "Created new synth/ directory."

# Iterate over all .v files in rtl/
for file in rtl/*.v; do
    filename=$(basename -- "$file")
    filename_noext="${filename%.v}"
    synth_file="synth/${filename_noext}_synth.v"
    
    # Detect module name (assuming first 'module' keyword in file defines it)
    module_name=$(grep -oP '(?<=module )\w+' "$file" | head -n 1)
    
    if [ -z "$module_name" ]; then
        echo "Warning: No module found in $file. Skipping."
        continue
    fi
    
    echo "Processing $file (Module: $module_name) -> $synth_file"
    
    # Run Yosys synthesis
    yosys -p "
    	read_verilog -sv -defer $file;
	hierarchy -top $module_name;
	flatten $module_name;
	opt;
	synth -top $module_name;
	dfflibmap -liberty $LIBERTY_FILE;
	printattrs;
	stat;
	abc -liberty $LIBERTY_FILE  -dff -keepff -g aig;
	stat;
        write_verilog $synth_file;
    "
    if [ $? -eq 0 ]; then
	    echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
	    echo "          Synthesis using Yosys completed.          "
	    echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
    else
	    echo "~~~~~~~~~~~~~ ERROR: YOSYS synthesis failed!  ~~~~~~~~~~~"
	    exit 1
    fi

done

























