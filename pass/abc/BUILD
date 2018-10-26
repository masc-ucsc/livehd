#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

cc_library(
    name = "pass_abc",
    srcs = glob(["*.cpp"],
                exclude = [
                    "dump_aig.cpp",
                    ]),
    hdrs = glob(["*.hpp"],
                exclude = [
                    "dump_aig.hpp",
                    ]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
        "//meta:meta",
        "@abc//:abc",
    ],
)

sh_library(
    name = "scripts",
    srcs = [
        "abc_check",
    ],
)

sh_test(
    name = "abc.sh",
    srcs = ["tests/abc.sh"],
    data = [
        "//inou/yosys:liblgraph_yosys.so",
        "//inou/yosys:verilog_tests",
        "//main:lgshell",
        ],
    deps = [
        "//inou/yosys:scripts",
        "//inou/tech:tech_gens",
        ":scripts",
    ]
)

