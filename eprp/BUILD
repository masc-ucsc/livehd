#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("//:copt_default.bzl", "COPTS")

cc_library(
    name = "eprp",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    copts = COPTS,
    deps = ["//elab:elab",
            "@com_google_absl//absl/container:flat_hash_map",
            "//task:task",
    ]
)

cc_test(
    name = "eprp_test",
    srcs = ["tests/eprp_test.cpp"],
    deps = [
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/strings",
        ":eprp",
    ],
)

