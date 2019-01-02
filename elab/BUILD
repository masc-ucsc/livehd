#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "elab",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = ["@fmt//:fmt",
            "@com_google_absl//absl/container:flat_hash_map",
            "@com_google_absl//absl/container:flat_hash_set",
    ],
    linkopts = ["-lpthread"],
)

filegroup(
    name = "elab_scripts",
    srcs = ["tests/synth1.eprp"],
)

cc_test(
    name = "elab_scanner_test",
    srcs = ["tests/elab_scanner_test.cpp"],
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
    name = "elab_verilog_test",
    srcs = ["tests/elab_verilog_test.cpp"],
    args = ["$(location :elab_scripts)"],
    deps = [
        ":elab",
    ],
    data = [
        ":elab_scripts",
    ],
)

cc_binary(
    name = "elab_pyrope_test",
    srcs = ["elab_pyrope_test.cpp"],
    # args = ["$(location tests/synth1.elab)"],
    deps = [
        ":elab",
    ],
)

