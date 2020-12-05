#!/bin/bash
#WARNING! This is a experimental and hardcoded script for Sheng-Hong environment only

pts='GCD Trivial TrivialArith Test1 Test2 Test3 Test4 Test5 Test6 SimpleBitOps Register RegisterSimple Flop'

CHISEL_PATH=~/chisel/
RESULT_PATH=~/chisel/results/
FIRRTL_EXE=~/firrtl/utils/bin/firrtl
LIVEHD_FIRRTL_PATH=~/livehd/inou/firrtl/tests

cd $CHISEL_PATH
for pt in $pts
do
  sbt "runMain chisel3.stage.ChiselMain --module design.${pt}"  

  $FIRRTL_EXE -i ${pt}.fir -X high
  $FIRRTL_EXE -i ${pt}.fir -X low
  $FIRRTL_EXE -i ${pt}.fir -X verilog
  $FIRRTL_EXE -i ${pt}.fir -X none --custom-transforms firrtl.transforms.WriteChPB
  $FIRRTL_EXE -i ${pt}.fir -X none --custom-transforms firrtl.transforms.WriteHighPB
  $FIRRTL_EXE -i ${pt}.fir -X none --custom-transforms firrtl.transforms.WriteLowPB

  mkdir -p $RESULT_PATH${pt}
  mv *.pb  $RESULT_PATH${pt}
  mv *.fir $RESULT_PATH${pt}
  mv *.v   $RESULT_PATH${pt}

done

