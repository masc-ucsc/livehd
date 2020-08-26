#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

cc_library(
    name = "inou_pyrope",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
        "//pass/common:pass",
    ],
)


filegroup(
    name = "pyrope_tests",
    srcs = glob([
        "tests/*.prp", "tests/compiler/*.prp", "tests/compiler/verilog_gld/*.v"
    ]),
    visibility = ["//visibility:public"],
)


sh_test(
    name = "pyrope_test.sh",
    tags = ["fixme"],
    srcs = ["tests/pyrope_test.sh"],
    data = [
        "//inou/yosys:liblgraph_yosys.so",
        "//main:lgshell",
        ":pyrope_tests",
        "//inou/yosys:verilog_tests",
        ],
)


sh_test(
    name = "lnast_prp_test.sh",
    srcs = ["tests/lnast_prp_test.sh"],
    # tags = ["manual"],
    # tags = ["fixme"],
    data = [
        "//inou/yosys:liblgraph_yosys.so",
        "//main:lgshell",
        ":pyrope_tests"
        ],
    deps = [
        "//inou/yosys:scripts",
    ]
)


cc_binary(
    name = "prp_test",
    srcs = ["prp_test.cpp"],
    deps = [
        "@gtest//:gtest_main",
        ":inou_pyrope",
    ],
)

cc_binary(
    name = "ast_test",
    srcs = ["ast_test.cpp"],
    deps = [
        "@gtest//:gtest_main",
        ":inou_pyrope",
    ],
)

cc_binary(
    name = "prp_lnast_test",
    srcs = ["prp_lnast_test.cpp"],
    deps = [
        "@gtest//:gtest_main",
        ":inou_pyrope",
    ],
)

cc_test(
    name = "prp_unit_test",
    srcs = ["prp_unit_test.cpp"],
    deps = [
        "@gtest//:gtest_main",
        ":inou_pyrope",
    ],
)
