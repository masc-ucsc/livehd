#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "elab",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = ["@spdlog//:headers",
    #"//core:core",
    ]
)

filegroup(
    name = "elab_scripts",
    srcs = ["tests/synth1.eprp"],
)

cc_test(
    name = "eprp_scanner_test",
    srcs = ["tests/eprp_scanner_test.cpp"],
    args = ["$(location :elab_scripts)"],
    deps = [
        "@gtest//:gtest_main",
        ":elab",
    ],
    data = [
        ":elab_scripts",
    ],
)

cc_test(
    name = "eprp_verilog_test",
    srcs = ["tests/eprp_verilog_test.cpp"],
    args = ["$(location :elab_scripts)"],
    deps = [
        ":elab",
    ],
    data = [
        ":elab_scripts",
    ],
)

cc_binary(
    name = "eprp_pyrope_test",
    srcs = ["eprp_pyrope_test.cpp"],
    # args = ["$(location tests/synth1.eprp)"],
    deps = [
        ":elab",
    ],
)

