#!/bin/bash
# WARNING! To execute this script, you will have to clone Sheng-Hong's Chisel patterns at your home directory
# git clone https://github.com/swang203/chisel3
# also prepare the upstream firrrl at home directory
# git clone https://github.com/chipsalliance/firrtl



pts='GCD_3bits GCD Coverage Shifts AddNot Decrementer PlusAnd RegXor Trivial TrivialArith Test1 Test2 Test3 
     Test5 Test6 SimpleBitOps Register RegisterSimple Flop ICache Ops Rob RocketCore HwachaSequencer 
     BundleConnect MemoryController PlusAnd SubModule RWSmem Smem Smem_simple Blackbox Blackbox2 FPU'

pts='ShiftRegister SimpleALU Stack VecSearch Accumulator Counter 
     DynamicMemorySearch LFSR16 MaxN Memo Mul Mux4 RealGCD SingleEvenFilter VecShiftRegister VecShiftRegisterParam
     VecShiftRegisterSimple VendingMachine VendingMachineSwitch  Adder4 Adder ByteSelector FullAdder HiLoMultiplier Life 
     Parity ResetShiftRegister Risc LogShifter Cell'

pts='Life'

CHISEL_PATH=~/chisel3
FIRRTL_PATH=~/firrtl
CHIRRTL_SRC_PATH=~/chisel3/chisel_chirrtl
CHISEL_SRC_PATH=~/chisel3/src/main/scala/design
FIRRTL_EXE=~/firrtl/utils/bin/firrtl
LIVEHD_FIRRTL_PATH=~/livehd/inou/firrtl/tests



if [ ! -d $CHISEL_PATH ]; then
  echo "ERROR: no chisel/firrtl patterns not ready, do this first: git clone https://github.com/swang203/chisel3"
  exit 1
elif [ ! -d $FIRRTL_PATH ]; then
  echo "ERROR: firrtl compiler environment not ready, do this first: git clone https://github.com/chipsalliance/firrtl"
  exit 1
fi



cd $CHISEL_PATH
for pt in $pts
do
  echo "-------- Pattern: ${pt} ----------------"
  mkdir -p $CHIRRTL_SRC_PATH/${pt}

  # two paths as sometimes you don't have a scala source code ... 
  if [ -f $CHISEL_SRC_PATH/${pt}.scala ]; then
    echo "-------- Compile from Chisel source code --------"
    sbt "runMain chisel3.stage.ChiselMain --module design.${pt}"  
    cp -f $CHISEL_SRC_PATH/${pt}.scala $CHIRRTL_SRC_PATH/${pt}
    mv -f ${pt}.fir $CHIRRTL_SRC_PATH/${pt}
    rm -f ${pt}.anno.json
  else
    if [ ! -f $CHIRRTL_SRC_PATH/${pt}/${pt}.fir ]; then
      echo "ERROR: could not find ${pt}.fir in ${CHIRRTL_SRC_PATH} or ${pt}.scala in ${CHISEL_SRC_PATH}"
      exit 1
    fi
    echo "-------- No Chisel source code, compile from Chirrtl -------"
  fi

  $FIRRTL_EXE -i ${CHIRRTL_SRC_PATH}/${pt}/${pt}.fir -X verilog
  $FIRRTL_EXE -i ${CHIRRTL_SRC_PATH}/${pt}/${pt}.fir -X high
  $FIRRTL_EXE -i ${CHIRRTL_SRC_PATH}/${pt}/${pt}.fir -X low
  $FIRRTL_EXE -i ${CHIRRTL_SRC_PATH}/${pt}/${pt}.fir -X none --custom-transforms firrtl.transforms.WriteLowPB
  # $FIRRTL_EXE -i ${pt}.fir -X high
  # $FIRRTL_EXE -i ${pt}.fir -X none --custom-transforms firrtl.transforms.WriteChPB
  # $FIRRTL_EXE -i ${pt}.fir -X none --custom-transforms firrtl.transforms.WriteHighPB


  if [ ! -f $CHISEL_PATH/${pt}.fir ]; then
    echo "ERROR: Lower-level firrl ${pt}.fir is not generated"
    exit 1
  elif [ ! -f $CHISEL_PATH/${pt}*.pb ]; then
    echo "ERROR: Protobuf ${pt}.pb is not generated"
    exit 1
  elif [ ! -f $CHISEL_PATH/${pt}.v ]; then
    echo "ERROR: Verilog ${pt}.v is not generated"
    exit 1
  fi

  cp -f ${pt}.hi.fir $CHIRRTL_SRC_PATH/${pt}
  cp -f ${pt}.lo.fir $CHIRRTL_SRC_PATH/${pt}
  cp -f ${pt}.v      $CHIRRTL_SRC_PATH/${pt}

  mv -f ${pt}.lo.fir $LIVEHD_FIRRTL_PATH/firrtl
  mv -f ${pt}.lo.pb  $LIVEHD_FIRRTL_PATH/proto
  mv -f ${pt}.v      $LIVEHD_FIRRTL_PATH/verilog_gld/${pt}.gld.v
  rm -f ${pt}.fir 
done

echo "-------- finish --------"
