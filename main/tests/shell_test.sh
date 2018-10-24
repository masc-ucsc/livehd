
declare -a inputs=("trivial.v" "null_port.v" "simple_flop.v")

LGCHECK=./inou/yosys/lgcheck
YOSYS=./inou/yosys/lgyosys
LGSHELL=./bazel-bin/main/lgshell

TEST_DIR=./pass/dce/tests

if [ ! -f ${LGSHELL} ]; then
  if [ -f ./main/lgshell ]; then
    LGSHELL=./main/lgshell
  else
    echo "could not find lgshell on $(pwd)"
    exit 1
  fi
fi

SHELL_ODIR=./shell_test/
rm -rf ${SHELL_ODIR}
mkdir ${SHELL_ODIR}

for input in ${inputs[@]}
do

  base=${input%.*}
  echo "inou.yosys.tolg files:${input}" | ${LGSHELL}

  if [ $? -eq 0 ] && [ -f lgdb/lgraph_${base}_nodes ]; then
    echo "Successfully created graph from ${input}"
  else
    echo "${YOSYS} ./inou/yosys/tests/${input} -d"
    echo "FAIL: lgyosys parsing terminated with an error (testcase ${input})"
    exit 1
  fi

  echo "lgraph.open name:${name} |> inou.yosys.fromlg odir:${SHELL_ODIR}" | ${LGSHELL}

  if [ $? -eq 0 ] && [ -f ${SHELL_ODIR}/${name}.v ]; then
    echo "Successfully created verilog from graph ${input}"
  else
    echo ${YOSYS} -g${base} -h -d
    echo "FAIL: verilog generation terminated with an error (testcase ${input})"
    exit 1
  fi

done


