#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

cc_library(
    name = "lnast_slang",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["*.hpp"]),
    #alwayslink=1,
    visibility = ["//visibility:public"],
    deps = [
        "//core:core",
        "@slang//:slang",
    ],
)

cc_test(
    name = "rewriter",
    srcs = ["tests/rewriter.cpp"],
    deps = [
        ":lnast_slang",
        ],
    )

