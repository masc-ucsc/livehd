#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "pass_compiler",
    srcs = glob(["*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    alwayslink=True,
    deps = [
        # "//pass/bitwidth:pass_bitwidth",
        "//pass/cprop:pass_cprop",
        "//pass/gioc:pass_gioc",
        "//pass/bitwidth:pass_bitwidth",
        "//pass/firmap:pass_firmap",
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
        "//main:lgshell",
        "//inou/pyrope:pyrope_tests"
        ],
    deps = [
        "//inou/yosys:scripts",
    ]
)
