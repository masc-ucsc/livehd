#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "eprp",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = ["@spdlog//:headers",
    #"//core:core",
    ]
)

cc_test(
    name = "eprp_test",
    srcs = ["eprp_test.cpp"],
    deps = [
        "@gtest//:gtest_main",
        ":eprp",
    ],
)

cc_binary(
    name = "eprp_scanner_test",
    srcs = ["eprp_scanner_test.cpp"],
    # args = ["$(location tests/synth1.eprp)"],
    deps = [
        ":eprp",
    ],
)

cc_binary(
    name = "eprp_verilog_test",
    srcs = ["eprp_verilog_test.cpp"],
    # args = ["$(location tests/synth1.eprp)"],
    deps = [
        ":eprp",
    ],
)

cc_binary(
    name = "eprp_pyrope_test",
    srcs = ["eprp_pyrope_test.cpp"],
    # args = ["$(location tests/synth1.eprp)"],
    deps = [
        ":eprp",
    ],
)
