#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

cc_library(
    name = "inou_code_gen",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
        "//pass/common:pass",
    ],
    data = [
        "//inou/pyrope:pyrope_tests",
    ],
)

sh_test(
    name = "prp_v_LEC_test.sh",
    srcs = ["tests/prp_v_LEC_test.sh"],
    data = [
        "//inou/yosys:liblgraph_yosys.so",
        "//inou/pyrope:pyrope_tests",
        "//main:lgshell",
    ],
    deps = [
        "//inou/yosys:scripts",
    ]
)


