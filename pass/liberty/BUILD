# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")
load("//tools:copt_default.bzl", "COPTS")

# Liberty DFF-cell scan + model, shared with pass.abc (no Pass_plugin so it can be
# depended on without pulling the pass.liberty command registration).
cc_library(
    name = "liberty_dff",
    srcs = ["liberty_dff.cpp"],
    hdrs = ["liberty_dff.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//graph",
    ],
)

cc_library(
    name = "pass_liberty",
    srcs = glob(
        ["*.cpp"],
        exclude = [
            "*test*.cpp",
            "liberty_dff.cpp",
        ],
    ),
    hdrs = glob(
        ["*.hpp"],
        exclude = ["liberty_dff.hpp"],
    ),
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        ":liberty_dff",
        "//graph",
        "//pass/common:pass",
        "@abc",
    ],
    alwayslink = True,  # static Pass_plugin registration
)
