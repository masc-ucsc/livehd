#!/bin/bash

design=../test/gcd.v
#Lef=../test/NanGate_15nm_OCL.macro.lef
#Liberty=../test/NanGate_15nm_OCL_fast_conditional_ecsm.lib

YOSYS=./inou/yosys/lgyosys
ABC=./inou/abc/lgaig
CHECK=./inou/abc/abc_check
json=./inou/json/lgjson

rm -rf ./lgdb/ ./logs ./yosys-test *.json *.v *.blif

${YOSYS} --techmap ${design}
${ABC} --lgdb ./lgdb --graph_name gcd
${json} --lgdb ./lgdb --graph_name gcd_mapped
${CHECK} golden.blif abc_output/gcd_post.blif | grep "Successfully matched generated verilog with yosys elaborated verilog file"
if [ $? -eq 0 ]; then
   rm -rf temp.blif
   echo "Successfully matched generated verilog with yosys elaborated verilog file"
else
   rm -rf temp.blif
   echo "FAIL: Equivalence check failed"
   exit 1
fi
exit 0
