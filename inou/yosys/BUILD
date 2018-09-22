#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "lgyosys",
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
    srcs   = [":lgyosys", "//core:core","//meta:meta"],
    static = ["@boost//:system" , "@boost//:filesystem", "@boost//:program_options"],
    visibility = ["//visibility:public"],
)

sh_test(
    name = "yosys.sh",
    srcs = ["tests/yosys.sh"],
    data = ["liblgraph_yosys.so","lgyosys","lgcheck"] + glob(["tests/*.v"]),
)

