#!/bin/sh

mkdir cov
rm -f cov/coverage.*
bazel clean
bazel coverage -k //...
./inou/yosys/tests/yosys.sh
./inou/yosys/tests/synth.sh
./inou/json/tests/rand_json.sh
./inou/cfg/tests/pyrope.sh
./pass/abc/tests/abc.sh
./bazel-bin/eprp/eprp_test
./bazel-bin/core/thread_pool_test
./bazel-bin/third_party/misc/ezsat/testbench

#gcovr -r .  bazel-out/k8-fastbuild/bin/core bazel-out/k8-fastbuild/bin/meta bazel-out/k8-fastbuild/bin/cops bazel-out/k8-fastbuild/bin/main bazel-out/k8-fastbuild/bin/inou bazel-out/k8-fastbuild/bin/pass --html-details --html -o cov/coverage.html

