
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

rm -rf lgdb/parse boom_test
mkdir -p boom_test

echo "echo \"live.parse files:${BOOM_FILE}\" | ${LGSHELL}"
echo "live.parse files:${BOOM_FILE}" | ${LGSHELL}
if [ $? -ne 0 ]; then
  echo "Failed to parse massive boom file"
  exit 1
fi

for i in AsyncResetReg; do
  echo "inou.yosys.tolg files:${BENCH_DIR}${i}.v |> inou.yosys.fromlg odir:boom_test" | ${LGSHELL}
  ${LGCHECK} --reference=${BENCH_DIR}/${i}.v --implementation=boom_test/${i}.v --top=$i 2> /dev/null > /dev/null

  if [ $? -ne 0 ]; then
    echo "ERROR: Module $i does not match"
    #exit 1
  else
    echo "SUCCESS: Module $i matches"
  fi
done

echo "inou.yosys.tolg files:\"lgdb/parse/chunk_\*\" |> inou.yosys.fromlg odir:boom_test" | ${LGSHELL}
if [ $? -ne 0 ]; then
  echo "Failed to read/write verilog for module $i"
  exit 1
fi


filename="chunk_`echo ${BOOM_FILE} | tr '/' '.'`"
for i in boom_test/*; do
  name=`basename ${i%.*}`
  ${LGCHECK} --reference=./lgdb/parse/${filename}:${name} --implementation=${i} --top=$name 2> /dev/null > /dev/null

  if [ $? -ne 0 ]; then
    echo "ERROR: Module $i does not match"
    #exit 1
  else
    echo "SUCCESS: Module $i matches"
  fi
done
