# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")
load("@rules_shell//shell:sh_test.bzl", "sh_test")
load("//tools:copt_default.bzl", "COPTS")

config_setting(
    name = "use_profiling",
    values = {"define": "profiling=1"},
)

# LHD_LOG (core/log.hpp) is developer trace logging. It compiles to a no-op in
# -c opt and stays active everywhere else, so the macro define is OFF under the
# `opt` compilation mode and ON otherwise. `defines` (not local_defines)
# propagate to every dependent so the macro vanishes in opt across the tree.
config_setting(
    name = "opt_build",
    values = {"compilation_mode": "opt"},
)

cc_library(
    name = "core",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*test*.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    copts = ["-std=c++23"] + COPTS,
    defines = select({
        ":use_profiling": [
            "LIVEHD_PROFILING=1",
        ],
        "//conditions:default": [],
    }) + select({
        ":opt_build": [],
        "//conditions:default": ["LIVEHD_LOGGING=1"],
    }),
    includes = ["."],
    linkopts = ["-lpthread"],
    visibility = ["//visibility:public"],
    deps = [
        "@hhds//hhds:core",  # diag Span/excerpts ride the hhds source locator
        "@hlop//hlop:hlop",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/container:node_hash_map",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@iassert",
    ] + select({
        ":use_profiling": [
            "@com_google_perfetto//:perfetto",
            "@mimalloc",  # This only works in Linux, comment for yosys2
        ],
        "//conditions:default": [],
    }),
)

cc_test(
    name = "thread_pool_test",
    srcs = [
        "tests/concurrentqueue.hpp",
        "tests/thread_pool_test.cpp",
    ],
    # tags = ["long1"], # Run only with long1 set of tests
    deps = [
        ":core",
        "@googletest//:gtest_main",
    ],
)

cc_binary(
    name = "vcd_sample",
    srcs = [
        "tests/vcd_sample.cpp", "tests/vcd_sample.hpp"
    ],
    deps = [
        ":core",
    ],
)

# Vcd_reader smoke test: parse a small VCD ($scope/$var headers, timestamps,
# vector + scalar samples) and check the per-signal parity table vcd_sample
# derives from the value-change counts.
sh_test(
    name = "vcd_sample_test",
    srcs = ["tests/vcd_sample_test.sh"],
    data = [
        "tests/vcd_sample_signals.vcd",
        ":vcd_sample",
    ],
)

cc_test(
    name = "diag_test",
    srcs = [
        "tests/diag_test.cpp",
    ],
    deps = [
        ":core",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "log_test",
    srcs = [
        "tests/log_test.cpp",
    ],
    deps = [
        ":core",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "pool_bench",
    srcs = [
        "tests/concurrentqueue.hpp",
        "tests/pool_bench.cpp",
    ],
    deps = [
        ":core",
        "@google_benchmark//:benchmark",
    ],
)

# lconst tests removed; equivalent coverage lives in @hlop//hlop:dlop_test
