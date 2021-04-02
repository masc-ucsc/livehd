# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "inou_lefdef",
    srcs = glob(
        ["*.cpp"],
        exclude = [
            "lglefdef.cpp",
            "defrw.cpp",
            "lefrw.cpp",
        ],
    ),
    hdrs = glob(["*.hpp"]),
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [
        "//pass/common:pass",
        "//third_party/misc/def/def",
        "//third_party/misc/lef/lef",
    ],
    alwayslink = True,
)

#cc_binary(
#    name = "lglefdef",
#    srcs = ["lglefdef.cpp"],
#    linkopts = ['-lpthread'],
#    deps = [
#        ":inou_lefdef",
#        "//inou/json:inou_json",
#        "@boost//:filesystem", "@boost//:program_options",
#    ],
#)
#
#cc_binary(
#    name = "defrw",
#    srcs = ["defrw.cpp"],
#    linkopts = ['-lpthread'],
#    deps = [
#        ":inou_lefdef",
#        "//inou/json:inou_json",
#    ],
#)
#
#cc_binary(
#    name = "lefrw",
#    srcs = ["lefrw.cpp"],
#    linkopts = ['-lpthread'],
#    deps = [
#        ":inou_lefdef",
#        "//inou/json:inou_json",
#    ],
#)
