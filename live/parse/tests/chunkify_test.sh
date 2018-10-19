#!/bin/bash


LGSHELL=./bazel-bin/main/lgshell

if [ ! -f $LGSHELL ]; then
  if [ -f ./main/lgshell ]; then
    LGSHELL=./main/lgshell
    echo "lgshell is in $(pwd)"
  else
    echo "FAILED: could not find lgshell binary in $(pwd)";
  fi
fi

echo "files path:inou/yosys/tests match:\"v$\" |> live.parse path:tmp1" | $LGSHELL

N1=$(grep module inou/yosys/tests/*.v | grep -v endmodule | wc -l)
N2=$(grep module tmp1/parse/chunk* | grep -v endmodule | wc -l)

if [ $N1 -ne $N2 ]; then
  echo "FAILED: yosys/tests inconsistent number of modules detected by live parse orig:"$N1" vs live:"$N2
  exit -3
else
  echo "PASS: yosys/tests live parse orig:"$N1" vs live:"$N2
fi

N1=$(ls -al tmp1/parse/file* | wc -l)
N2=$(ls -al inou/yosys/tests/*.v | wc -l)
if [ $N1 -ne $N2 ]; then
  echo "FAILED: yosys/tests inconsistent number of files. It should be "$N2", not "$N1
  exit -3
fi

echo "live.parse files:projects/boom/boom.system.TestHarness.BoomConfig.v path:tmp2" | $LGSHELL
N1=$(grep module projects/boom/boom.system.TestHarness.BoomConfig.v | grep -v endmodule | wc -l)
N2=$(grep module tmp2/parse/chunk* | grep -v endmodule | wc -l)

if [ $N1 -ne $N2 ]; then
  echo "FAILED: boom inconsistent number of modules detected by live parse orig:"$N1" vs live:"$N2
  exit -3
else
  echo "PASS: boom live parse orig:"$N1" vs live:"$N2
fi

N1=$(ls -al tmp2/parse/file* | wc -l)
N2="1"
if [ $N1 -ne $N2 ]; then
  echo "FAILED: boom inconsistent number of files. It should be "$N2", not "$N1
  exit -3
fi

exit 0

