
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
  file="inou/yosys/tests/${input}"
  if [ ! -f $file ]; then
    echo "shell_test.sh: ERROR, could not access files:${file}"
    exit 3
  fi

  echo "shell_test.sh: files:${file} name:${base}"
  echo "inou.yosys.tolg files:${file}" | ${LGSHELL}

  if [ $? -eq 0 ] && [ -f lgdb/lgraph_${base}_nodes ]; then
    echo "Successfully created graph from ${input}"
  else
    echo "${YOSYS} ./inou/yosys/tests/${input} -d"
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

  echo "shell_test.sh: name:${base} odir:${SHELL_ODIR}"
  echo "lgraph.open name:${base} |> dump |> inou.yosys.fromlg odir:${SHELL_ODIR}" | ${LGSHELL}

  if [ $? -eq 0 ] && [ -f ${SHELL_ODIR}/${base}.v ]; then
    echo "Successfully created verilog from graph ${file}"
  else
    echo ${YOSYS} -g${base} -h -d
    echo "FAIL: verilog generation terminated with an error (testcase ${file})"
    exit 1
  fi

done

