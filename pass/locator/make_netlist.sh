#!/bin/bash

#check that sky130.lib is present
# |edit run.sh (VERILOG_FILES path)
# |source run.sh
# |yosys -c synth.tcl

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
echo "** ENSURED CORRECT DESIGN NAME IN RUN.SH?**"

mv ./*0.chk.rpt out/.
mv ./*0.stat.rpt out/.
mv ./*_pre.stat out/.
mv ./*_dff.stat out/.


