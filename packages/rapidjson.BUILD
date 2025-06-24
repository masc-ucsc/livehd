# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "headers",
    hdrs = glob(["rapidjson/**/*.h"]),
    #includes = ["include"],
    copts = [
        "-w",
        "-O2",
    ],
    visibility = ["//visibility:public"],
)
