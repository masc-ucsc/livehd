#!/bin/bash
#WARNING! This is a experimental and hardcoded scrip for Sheng-Hong use only

pts='GCD'

CHISEL_PATH=~/chisel/
FIRRTL_PATH=~/firrtl/protobuf/
LIVEHD_FIRRTL_PATH=~/livehd/inou/firrtl/tests

cd $CHISEL_PATH
for pt in $pts
do
  OLDDIR=$(ls -td ./test_run_dir/*/ | head -1)
  echo $OLDDIR
  rm -rf $OLDDIR

  sbt "test:runMain ${pt,,}.${pt}Main"  
  NEWDIR=$(ls -td ./test_run_dir/*/ | head -1)
  cd $NEWDIR

  mkdir -p $FIRRTL_PATH${pt}
  cp ${pt}.fir $FIRRTL_PATH${pt}

  cd $FIRRTL_PATH${pt}
  ../../utils/bin/firrtl -i ${pt}.fir -X high
  ../../utils/bin/firrtl -i ${pt}.fir -X low
  ../../utils/bin/firrtl -i ${pt}.fir -X verilog
  ../../utils/bin/firrtl -i ${pt}.fir -X none --custom-transforms firrtl.transforms.WriteChPB
  ../../utils/bin/firrtl -i ${pt}.fir -X none --custom-transforms firrtl.transforms.WriteHighPB
  ../../utils/bin/firrtl -i ${pt}.fir -X none --custom-transforms firrtl.transforms.WriteLowPB

done

