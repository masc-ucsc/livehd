load("@rules_cc//cc:defs.bzl", "cc_proto_library")
load("@rules_java//java:defs.bzl", "java_library")
load("@rules_proto//proto:defs.bzl", "proto_library")


proto_library(
    name = "firrtl_proto",
    srcs = ["firrtl.proto"],
)

cc_proto_library(
    name = "firrtl_cc_proto",
    deps = [":firrtl_proto"],
)

cc_library(
    name = "inou_firrtl_cpp",
    srcs = [
        "inou_firrtl.cpp",
        "fir_tolnast.cpp",
        "lnast_tofir.cpp",
        "find_circuit_comps.cpp",
        "inou_firrtl.hpp",
    ],
    hdrs = ["inou_firrtl.hpp"],
    visibility = ["//visibility:public"],
    includes = ["."],
    alwayslink=True,
    deps = [
        ":firrtl_cc_proto",
        "//pass/common:pass",
    ],
)

sh_test(
    name = "firrtl_test_livehd.sh",
    srcs = ["tests/firrtl_test_livehd.sh"],
    data = [
        "//inou/yosys:verilog_tests",
        "//main:lgshell",
    ],
    deps = [
        "//inou/yosys:scripts"
    ]
)
