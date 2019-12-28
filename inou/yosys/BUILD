#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

cc_library(
    name = "lgyosys-lib",
    srcs = glob(["lg*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["lg*.hpp"]),
    #alwayslink=1,
    defines = ["_YOSYS_","YOSYS_ENABLE_PLUGINS"],
    visibility = ["//visibility:public"],
    deps = [
        "//core:core",
        "@yosys//:headers",
    ],
)

cc_library(
    name = "lgyosys-api",
    srcs = glob(["inou*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["inou*.hpp"]),
    visibility = ["//visibility:public"],
    deps = [
        "//core:core",
        ":lgyosys-lib",
        "@mustache//:headers",
    ],
    data = ["inou_yosys_read.ys",
            "inou_yosys_write.ys",
    ],
)

load("//tools:linkso.bzl", "linkso")

linkso(
    name = "liblgraph_yosys",
    srcs   = [
        ":lgyosys-lib",
        #"//core:core",
        #"//eprp:eprp",
        #"//elab:elab",
        #"@com_google_absl//absl/base:base",
        # "@com_google_absl//absl/base:malloc_internal",
#        "@com_google_absl//absl/hash:city",
        #"@com_google_absl//absl/container:flat_hash_map",
        #"@com_google_absl//absl/container:hashtablez_sampler",
        #"@com_google_absl//absl/container:raw_hash_set",
        #"@com_google_absl//absl/synchronization:synchronization",
        #"@com_google_absl//absl/debugging:symbolize",
    ],
    deps   = [
        "@com_google_absl//absl/container:flat_hash_map",
        "@fmt//:fmt",
        "//core:core",
        "//eprp:eprp",
        ],
    visibility = ["//visibility:public"],
)

sh_library(
    name = "scripts",
    srcs = [
        "lgyosys",
        "lgcheck",
    ],
    visibility = ["//visibility:public"],
)

filegroup(
    name = "verilog_tests",
    srcs = glob([
        "tests/*.v",
    ]),
    visibility = ["//visibility:public"],
)

