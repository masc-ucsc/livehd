#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "pass_compiler",
    srcs = glob(["*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
        # "//pass/bitwidth:pass_bitwidth",
        "//pass/cprop:pass_cprop",
        "//pass/bitwidth:pass_bitwidth",
        "//pass/lnast_tolg:pass_lnast_tolg",
        "//pass/common:pass",
        "//inou/graphviz:inou_graphviz",
    ],
)


sh_test(
    name = "pyrope_compile.sh",
    srcs = ["tests/pyrope_compile.sh"],
    # tags = ["manual"],
    # tags = ["fixme"],
    data = [
        "//inou/yosys:liblgraph_yosys.so",
        "//main:lgshell",
        "//inou/pyrope:pyrope_tests"
        ],
    deps = [
        "//inou/yosys:scripts",
    ]
)
