# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "inou_prp",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*test*.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//lnast",
        "//pass/common:pass",
        "//core:core",
        "@tree-sitter-pyrope//:tree-sitter-pyrope",
    ],
    alwayslink = True,
)

cc_binary(
    name = "ts_traverse_test",
    srcs = ["tests/ts_traverse_test.cpp"],
    copts = COPTS,
    deps = [
        ":inou_prp",
        # "@com_google_googletest//:gtest_main",
    ],
)

[
    sh_test(
        name = "prp-%s" % prp_file.split("/")[-1].split(".")[0],
        srcs = ["tests/pyrope_test.py"],
        args = [
            "-i",
            "$(location //inou/prp:%s)" % prp_file,
        ],
        data = [
            "//inou/prp:tests/prplib.py",
            "//inou/prp:tests/pyrope_test.py",
            "//inou/prp:%s" % prp_file,
            "//main:lgshell",
        ],
    )
    for prp_file in glob(["tests/pyrope/*.prp", "tests/comptime/*.prp"])
]
