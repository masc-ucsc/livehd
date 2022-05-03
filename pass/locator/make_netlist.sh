#!/bin/bash

#check that sky130.lib is present
# |edit run.sh (VERILOG_FILES path)
# |source run.sh
# |yosys -c synth.tcl

#rm ./*0.chk.rpt
#rm ./*0.stat.rpt
#rm ./*_pre.stat
#rm ./*_dff.stat
rm -r out/
rm netlist.v


if [ ! -f sky130.lib ]; then
  echo "Liberty file not found. follow README.md to see the steps to create the sky130.lib file"
  exit 1
fi

if [ ! -f run.sh ]; then
  echo "run.sh file not found!!"
  exit 1
fi

if [ ! -f synth.tcl ]; then
  echo "synth.tcl file not found."
  exit 1
fi

source ./run.sh
yosys -c synth.tcl

echo "** POST-SYNTH NETLIST FORMED:            **"
echo "** ./netlist.v                           **"
echo "** ENSURE CORRECT DESIGN NAME IN RUN.SH! **"

rm ./*0.chk.rpt
rm ./*0.stat.rpt
rm ./*_pre.stat
rm ./*_dff.stat


