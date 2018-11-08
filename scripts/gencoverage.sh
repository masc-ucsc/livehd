#!/bin/sh

mkdir cov
rm -f cov/coverage.*
bazel clean
bazel coverage -k //...

./inou/yosys/tests/yosys.sh
./inou/yosys/tests/synth.sh

./inou/json/tests/rand_json.sh

./inou/cfg/tests/pyrope.sh
./inou/pyrope/tests/pyrope_test.sh

./pass/abc/tests/abc.sh

./pass/dce/tests/dce.sh

./bazel-bin/pass/sample/sample_test2
./bazel-bin/pass/sample/tests/sample_test1.sh


./cops/live/tests/invariant.sh

./main/tests/lgshell_test.sh

./bazel-bin/eprp/eprp_test

./bazel-bin/core/thread_pool_test
./bazel-bin/core/iter_test
./bazel-bin/core/edge_test
./bazel-bin/core/dense_test
./bazel-bin/core/char_test

./bazel-bin/main/main_test

./bazel-bin/third_party/misc/ezsat/testbench

./bazel-bin/inou/liveparse/chunkify_verilog_test
./inou/liveparse/tests/chunkify_test.sh


echo " live.parse files:./test/benchmarks/boom/boombase.v path:tmp2 " | ./bazel-bin/main/lgshell
echo "files path:./inou/yosys/tests/ match:"\.v$" |> live.parse path:tmp2" | ./bazel-bin/main/lgshell

#gcovr -r .  bazel-out/k8-fastbuild/bin/core bazel-out/k8-fastbuild/bin/meta bazel-out/k8-fastbuild/bin/cops bazel-out/k8-fastbuild/bin/main bazel-out/k8-fastbuild/bin/inou bazel-out/k8-fastbuild/bin/pass --html-details --html -o cov/coverage.html

