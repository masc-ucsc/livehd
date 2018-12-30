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

./main/tests/lgshell_test.sh

./bazel-bin/eprp/eprp_test

./bazel-bin/core/iter_test
./bazel-bin/core/edge_test

./bazel-bin/task/thread_pool_test

./bazel-bin/meta/lgraph_each

./bazel-bin/lmms/dense_test
./bazel-bin/lmms/dense_bench
./bazel-bin/lmms/char_test

./bazel-bin/main/main_test

./bazel-bin/third_party/misc/ezsat/testbench

./bazel-bin/inou/liveparse/chunkify_verilog_test
./inou/liveparse/tests/chunkify_test.sh

./cops/live/tests/invariant.sh

