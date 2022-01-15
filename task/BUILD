# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

config_setting(
    name = "use_profiling",
    values = {"define": "profiling=1"},
)

cc_library(
    name = "task",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*test*.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    copts = COPTS,
    defines = select({
        ":use_profiling": [
            "LIVEHD_PROFILING=1",
        ],
        "//conditions:default": [],
    }),
    includes = ["."],
    linkopts = ["-lpthread"],
    visibility = ["//visibility:public"],
    deps = [
        "//mmap_lib:headers",
    ] + select({
        ":use_profiling": [
            "@com_google_perfetto//:perfetto",
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
        ":task",
        "@com_google_googletest//:gtest_main",
    ],
)
