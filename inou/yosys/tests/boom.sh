
OPT_LGSHELL=./bazel-bin/main/lgshell
YOSYS=./inou/yosys/lgyosys
LGCHECK=./inou/yosys/lgcheck

BENCH_DIR=./projects/boom/
TEST_CASE=boom.system.TestHarness.BoomConfig.v
BOOM_FILE=${BENCH_DIR}${TEST_CASE}

if [ ! -f ${OPT_LGSHELL} ]; then
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


echo "live.parse files:${BOOM_FILE}" | ${LGSHELL}

for i in lgdb/parse/*; do
  echo "inou.yosys.tolg files:${i} |> inou.yosys.fromlg odir:tmp" | ${LGSHELL}

  if [ $? -ne 0 ]; then
    echo "Failed to read/write verilog for module $i"
    exit 1
  fi
done

filename="chunk_`echo ${BOOM_FILE} | tr '/' '.'`"
for i in tmp/*; do
  name=`basename ${i%.*}`
  ${LGCHECK} --reference=./lgdb/parse/${filename}:${name} --implementation=${i} --top=$name

  if [ $? -ne 0 ]; then
    echo "Module $i does not match"
    exit 1
  fi
done
