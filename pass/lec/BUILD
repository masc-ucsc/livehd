#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "pass_lec",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
        "//core:core",
        "//third_party/misc/ezsat:ezsat",
    ]
)

cc_test(
    name = "lec_test1",
    srcs = ["lec_test1.cpp"],
    deps = [
        "@gtest//:gtest_main",
        ":pass_lec",
    ],
)
