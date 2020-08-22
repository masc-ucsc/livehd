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

rm -rf mlgdb
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

  echo "inou.yosys.tolg files:${file} path:mlgdb |> pass.bitwidth hier:true" | ${LGSHELL}

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

  echo "lgraph.open name:${base} path:mlgdb |> dump |> inou.yosys.fromlg odir:${SHELL_ODIR} |> inou.graphviz.from" | ${LGSHELL}

  if [ $? -eq 0 ] && [ -f ${SHELL_ODIR}/${base}.v ]; then
    echo "Successfully created verilog from graph ${file}"
  else
    echo "FAIL: verilog generation terminated with an error (testcase ${file})"
    exit 1
  fi
done


echo "inou.yosys.tolg files:inou/yosys/tests/trivial.v path:mlgdb" | ${LGSHELL}
echo "inou.yosys.tolg files:inou/yosys/tests/trivial.v path:mlgdb |> pass.cprop" | ${LGSHELL}
echo "lgraph.open name:trivial path:mlgdb |> lgraph.stats" | ${LGSHELL} | sed -es/trivial/potato/g >potato1.log
if [ $? -ne 0 ]; then
  echo "FAIL: it should open trivial"
  exit 1
fi

echo "lgraph.copy   name:trivial path:mlgdb dest:temp_lg" | ${LGSHELL}
echo "lgraph.rename name:temp_lg path:mlgdb dest:potato" | ${LGSHELL}
echo "lgraph.open name:potato path:mlgdb |> lgraph.stats" | ${LGSHELL} >potato2.log
if [ $? -ne 0 ]; then
  echo "FAIL: it could not rename to potato"
  exit 1
fi
$(/usr/bin/diff potato1.log potato2.log)
if [ $? -ne 0 ]; then
  echo "FAIL: the diff for potato should match"
  exit 1
fi

exit 0

