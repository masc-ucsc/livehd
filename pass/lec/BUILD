#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("//:copt_default.bzl", "COPTS")

cc_library(
    name = "pass_lec",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    alwayslink=True,
    copts = COPTS,
    deps = [
        "//pass/common:pass",
        "//third_party/misc/ezsat:ezsat",
        "@boolector//:boolector",
        "@cryptominisat//:cryptominisat",
    ]
)

cc_test(
    name = "lec_test1",
    srcs = ["lec_test1.cpp"],
    deps = [
        "@com_google_googletest//:gtest_main",
        ":pass_lec",
    ],
)

cc_test(
    name = "boolector_test",
    srcs = ["boolector_test.cpp"],
    deps = [
        "@boolector//:boolector",
        "@cryptominisat//:cryptominisat",
    ],
)
