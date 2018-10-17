
bazel coverage -k //...
./bazel-bin/third_party/misc/ezsat/testbench
./pass/abc/tests/abc.sh
./inou/yosys/tests/yosys.sh
./inou/yosys/tests/synth.sh
./inou/json/tests/rand_json.sh
./bazel-bin/eprp/eprp_test
./bazel-bin/core/thread_pool_test

gcovr -r .  bazel-out/k8-fastbuild/bin/core bazel-out/k8-fastbuild/bin/meta bazel-out/k8-fastbuild/bin/cops bazel-out/k8-fastbuild/bin/main bazel-out/k8-fastbuild/bin/inou bazel-out/k8-fastbuild/bin/pass --html-details --html -o cov/coverage.html

