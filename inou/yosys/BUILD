#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "lgyosys-lib",
    srcs = glob(["lgyosys*.cpp"]),
    hdrs = glob(["lgyosys*.hpp"]),
    #alwayslink=1,
    defines = ["_YOSYS_","YOSYS_ENABLE_PLUGINS"],
    visibility = ["//visibility:public"],
    deps = [
        "//meta:meta",
        "@yosys//:headers",
    ],
)

load("//tools:linkso.bzl", "linkso")

linkso(
    name = "liblgraph_yosys",
    srcs   = [
        ":lgyosys-lib",
        "//core:core",
        "//meta:meta",
    ],
    static = [" "],
    visibility = ["//visibility:public"],
)

sh_library(
    name = "scripts",
    srcs = [
        "lgyosys",
        "lgcheck",
    ],
)

filegroup(
    name = "verilog_tests",
    srcs = glob([
        "tests/*.v",
    ]),
)

sh_test(
    name = "yosys.sh",
    srcs = ["tests/yosys.sh"],
    data = [
        "liblgraph_yosys.so",
        ":verilog_tests",
        ],
    deps = [
        ":scripts",
    ]
)

sh_test(
    name = "synth.sh",
    srcs = ["tests/synth.sh"],
    data = [
        "liblgraph_yosys.so",
        ":verilog_tests",
        ],
    deps = [
        ":scripts",
        "//inou/tech:tech_gens",
    ],
)

