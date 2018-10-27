
LGSHELL=./bazel-bin/main/lgshell
YOSYS=./inou/yosys/lgyosys
LGCHECK=./inou/yosys/lgcheck

BENCH_DIR=./projects/boom/
TEST_CASE=boom.system.TestHarness.BoomConfig.v
BOOM_FILE=${BENCH_DIR}${TEST_CASE}

if [ ! -f ${LGSHELL} ]; then
  echo "boom test could not find lgshell"
  exit 1
fi

if [ ! -f ${YOSYS} ]; then
  echo "boom test could not find lgyosys"
  exit 1
fi

if [ ! -f ${LGCHECK} ]; then
  echo "boom test could not find lgcheck"
  exit 1
fi

TEST_OUT=boom_test
rm -rf lgdb/parse ${TEST_OUT}
mkdir -p ${TEST_OUT}

echo "echo \"live.parse files:${BOOM_FILE}\" | ${LGSHELL}"
echo "live.parse files:${BOOM_FILE}" | ${LGSHELL}
if [ $? -ne 0 ]; then
  echo "Failed to parse massive boom file"
  exit 1
fi

for i in AsyncResetReg; do
  echo "inou.yosys.tolg files:${BENCH_DIR}${i}.v |> inou.yosys.fromlg odir:${TEST_OUT}" | ${LGSHELL}
  ${LGCHECK} --reference=${BENCH_DIR}/${i}.v --implementation=${TEST_OUT}/${i}.v --top=$i 2> /dev/null > /dev/null

  if [ $? -ne 0 ]; then
    echo "ERROR: Module $i does not match"
    #exit 1
  else
    echo "SUCCESS: Module $i matches"
  fi
done

#echo "files path:./lgdb/parse match:\"chunk.*\" |> inou.yosys.tolg |> inou.yosys.fromlg odir:boom_test" | ${LGSHELL}
echo "files path:./lgdb/parse match:\"chunk.*\" |> inou.yosys.tolg top:ExampleBoomSystem" | ${LGSHELL}
if [ $? -ne 0 ]; then
  echo "Failed to read/write verilog for module $i"
  exit 1
fi

for i in lgdb/parse/chunk*; do
  name=${i##*:};
  echo "lgraph.open name:${name} |> inou.yosys.fromlg odir:boom_test" | ${LGSHELL}
  if [ $? -ne 0 ]; then
    echo "Failed to read/write verilog for module $name"
  fi
done

filename="chunk_`echo ${BOOM_FILE} | tr '/' '.'`"
for i in ${TEST_OUT}/*; do
  name=`basename ${i%.*}`
  ${LGCHECK} --reference=./lgdb/parse/${filename}:${name} --implementation=${i} --top=$name 2> /dev/null > /dev/null

  if [ $? -ne 0 ]; then
    echo "ERROR: Module $name does not match"
    #exit 1
  else
    echo "SUCCESS: Module $name matches"
  fi
done

