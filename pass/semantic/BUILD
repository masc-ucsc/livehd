#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "semantic_pass",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
        "//pass/common:pass",
        "//elab:elab",
    ]
)

cc_test(
    name = "lnast_semantic_test",
    srcs = ["tests/lnast_semantic_test.cpp",],
    deps = [
        ":semantic_pass",
        ],
    )