#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "pass_sat_opt",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    alwayslink=True,
    deps = [
        "//pass/common:pass",
        "//third_party/misc/ezsat:ezsat",
    ]
)

cc_test(
    name = "sat_opt_test1",
    srcs = ["sat_opt_test1.cpp"],
    deps = [
        "@gtest//:gtest_main",
        ":pass_sat_opt",
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
