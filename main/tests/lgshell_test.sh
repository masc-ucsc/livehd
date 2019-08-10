#!/bin/bash

LGSHELL=./bazel-bin/main/lgshell

if [ ! -f ${LGSHELL} ]; then
  if [ -f ./main/lgshell ]; then
    LGSHELL=./main/lgshell
  else
    echo "could not find lgshell on $(pwd)"
    exit 1
  fi
fi

#basic testing
echo "exit" | ${LGSHELL}
if [ $? -ne 0 ] ; then
  exit 1
fi

echo "exit" | ${LGSHELL} -q
if [ $? -ne 0 ] ; then
  exit 1
fi

for cmd in "quit" "help" "help inou.yosys.tolg"
do
  echo "${cmd}" | ${LGSHELL} -q
  if [ $? -ne 0 ] ; then
    echo "lgshell_test.sh ${cmd} failed"
    exit 1
  fi
done

# call inous / passes
SHELL_ODIR=./shell_test/
rm -rf ${SHELL_ODIR}
mkdir ${SHELL_ODIR}

mkdir -p mlgdb

declare -a inputs=("trivial.v" "mux.v" "hierarchy.v")
for input in ${inputs[@]}
do
  base=${input%.*}
  file="inou/yosys/tests/${input}"
  if [ ! -f $file ]; then
    echo "shell_test.sh: ERROR, could not access files:${file}"
    exit 3
  fi

  echo "inou.yosys.tolg files:${file} path:mlgdb" | ${LGSHELL}

  if [ $? -eq 0 ]; then
    echo "Successfully created graph from ${input}"
  else
    echo "FAIL: lgyosys parsing terminated with an error (testcase ${file})"
    exit 1
  fi
done

for input in ${inputs[@]}
do
  base=${input%.*}
  file="inou/yosys/tests/${input}"
  if [ ! -f $file ]; then
    echo "shell_test.sh: ERROR, could not access files:${file}"
    exit 3
  fi

  echo "lgraph.open name:${base} path:mlgdb |> dump |> inou.yosys.fromlg odir:${SHELL_ODIR} |> inou.graphviz.fromlg" | ${LGSHELL}

  if [ $? -eq 0 ] && [ -f ${SHELL_ODIR}/${base}.v ]; then
    echo "Successfully created verilog from graph ${file}"
  else
    echo "FAIL: verilog generation terminated with an error (testcase ${file})"
    exit 1
  fi
done

echo "lgraph.open name:trivial path:mlgdb |> lgraph.stats" | ${LGSHELL}
if [ $? -ne 0 ]; then
  echo "FAIL: it should open trivial"
  exit 1
fi

exit 0

