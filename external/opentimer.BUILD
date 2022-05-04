
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "opentimer",
    hdrs = glob(["ot/*.hpp", "ot/**/*.hpp"]),
    copts = [
        "-w",
        "-O2",
    ],
    srcs = glob(["lib/**/*.cpp"]),
    includes = ["."],
    visibility = ["//visibility:public"],
)

