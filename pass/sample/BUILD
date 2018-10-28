#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "pass_sample",
    srcs = glob(["*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
        "//meta:meta",
        "//core:core",
    ]
)

sh_test(
    name = "pass_test1.sh",
    srcs = ["tests/pass_test1.sh"],
    data = [
        "//inou/yosys:liblgraph_yosys.so",
        "//main:lgshell",
        ],
)

