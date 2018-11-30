#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "eprp",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = ["@spdlog//:headers",
            "//elab:elab",
            "//task:task",
    ]
)

cc_test(
    name = "eprp_test",
    srcs = ["tests/eprp_test.cpp"],
    deps = [
        "@gtest//:gtest_main",
        ":eprp",
    ],
)

