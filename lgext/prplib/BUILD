load("@rules_cc//cc:defs.bzl", "cc_library")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "prplib",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*test*.cpp"],
    ),
    # hdrs = glob(["*.hpp"]),
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//lgext/common:lgcpp",
    ],
)
