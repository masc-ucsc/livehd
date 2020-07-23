#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

cc_library(
    name = "slang",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*test*.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    linkopts = ["-lstdc++fs"],  # Needed for gcc 7-8
    #alwayslink=1,
    visibility = ["//visibility:public"],
    deps = [
        "//core",
        "@slang",
    ],
)

cc_binary(
    name = "rewriter",
    srcs = ["tests/rewriter.cpp"],
    deps = [
        ":slang",
        ],
    )
