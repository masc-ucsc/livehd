load("@rules_cc//cc:defs.bzl", "cc_library", "cc_proto_library")
load("@rules_proto//proto:defs.bzl", "proto_library")
load("//tools:copt_default.bzl", "COPTS")

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
        "find_circuit_comps.cpp",
        "fir_tolnast.cpp",
        "inou_firrtl.cpp",
        "inou_firrtl.hpp",
        "lnast_tofir.cpp",
    ],
    hdrs = ["inou_firrtl.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        ":firrtl_cc_proto",
        "//pass/common:pass",
    ],
    alwayslink = True,
)

filegroup(
    name = "firrtl_tests",
    srcs = glob([
        "tests/proto/*.pb",  # Protobuf tests
        "tests/verilog_gld/*.v",  # Verilog golden models
    ]) + [
        "post_io_renaming.py",  # Script to match livehd to firrtl names
    ],
    visibility = ["//visibility:public"],
)
