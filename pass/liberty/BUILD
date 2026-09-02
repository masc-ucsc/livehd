# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

# The DFF-cell picker's sources, hashed into //pass/abc:abc_salt: the pick
# (cell name, QN-as-Q polarity, drive ladder) is baked into every cached mapped
# body, so a change here must invalidate the incremental cache like a mapper
# change does.
filegroup(
    name = "liberty_dff_srcs",
    srcs = [
        "liberty_dff.cpp",
        "liberty_dff.hpp",
    ],
    visibility = ["//pass/abc:__pkg__"],
)

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

# The DFF-cell parse/rank contract on synthetic Liberty text shaped like the
# PDKs pass.abc maps against (ASAP7 QN family, sky130 dfxtp/dfxbp), the negedge
# and clear/preset rejections, and the area ranking.
cc_test(
    name = "liberty_dff_test",
    size = "small",
    srcs = ["liberty_dff_test.cpp"],
    copts = COPTS,
    deps = [
        ":liberty_dff",
        "@googletest//:gtest_main",
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
