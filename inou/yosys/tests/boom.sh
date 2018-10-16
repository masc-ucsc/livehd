
OPT_LGSHELL=./bazel-bin/main/lgshell

if [ ! -f ${OPT_LGSHELL} ]
  echo "boom test could not find lgshell"
  exit 1
fi

echo "inou.yosys.tolg files:./test/benchmarks/boom/boombase.v |> inou.yosys.fromlg odir:tmp" | ./bazel-bin/main/lgshell

for i in tmp/*; do
  name=`basename ${i%.*}`
  ./inou/yosys/lgcheck --reference=./test/benchmark/boom/boombase.v --implementation=$i --top=$name;
  ./inou/yosys/lgcheck --reference=./test/benchmarks/boom/boombase.v --implementation=./tmp/ --top=$name
done
