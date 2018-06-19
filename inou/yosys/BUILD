cc_library(
    name = "inou_yosys",
    srcs = glob(["*.cpp"]),
    hdrs = glob(["*.hpp"]),
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
    srcs   = [":inou_yosys", "//core:core","//meta:meta"],
    static = ["@boost//:system" , "@boost//:filesystem", "@boost//:program_options"],
)

