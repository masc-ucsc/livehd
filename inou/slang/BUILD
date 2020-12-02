#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

cc_library(
    name = "inou_slang",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*test*.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    alwayslink=True,
    deps = [
        "//pass/common:pass",
        "@slang",
    ],
)

cc_binary(
    name = "rewriter",
    srcs = ["tests/rewriter.cpp"],
    deps = [
        ":inou_slang",
        ],
    )

cc_binary(
    name = "driver",
    srcs = ["tests/driver.cpp"],
    deps = [
        ":inou_slang",
        ],
    )
