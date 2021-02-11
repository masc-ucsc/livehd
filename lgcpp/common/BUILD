
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "lgcpp",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    copts = COPTS,
    deps = [
        "//core:core",
        "//pass/common:pass",
    ]
)

