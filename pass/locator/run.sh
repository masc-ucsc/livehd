
#export VERILOG_FILES=../../opentimer_tests/arith.v
#export DESIGN_NAME=arith

name=firrtl_gcd_3bits

export VERILOG_FILES=../../pre_synth/${name}.v
export DESIGN_NAME=$name

export SYNTH_BUFFERING=0
export SYNTH_SIZING=0
export LIB_SYNTH=sky130.lib
export LIB_SYNTH_COMPLETE_NO_PG=sky130.lib

# In ns
export CLOCK_PERIOD=10

export SYNTH_DRIVING_CELL="sky130_fd_sc_hs__inv_1"
export SYNTH_CAP_LOAD=33.3
export SYNTH_MAX_FANOUT=5

export SYNTH_STRATEGY='DELAY 0'

export synthesis_tmpfiles=out
export synth_report_prefix=$DESIGN_NAME

export SYNTH_ADDER_TYPE="YOSYS"

export SYNTH_NO_FLAT=0

export SYNTH_SHARE_RESOURCES=0

export SAVE_NETLIST=netlist.v

mkdir -p out

