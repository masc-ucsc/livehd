# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "mockturtle",
    srcs = glob(["lib/**/*.cpp"]) + glob([
        "lib/**/*.hpp",
    ]) + glob([
        "lib/**/*.h",
    ]),
    hdrs = glob(["include/**/*.hpp"]),
    copts = [
        "-w",
        "-O2",
    ],
    defines = [
        "LIN64",
        "ABC_NAMESPACE=pabc",
        "DISABLE_NAUTY",
    ],
    includes = [
        "include",
        "lib/abcesop",
        "lib/abcresub",
        "lib/abcsat",
        "lib/bill",
        "lib/fmt",
        "lib/kitty",
        "lib/lorina",
        "lib/matplot",
        "lib/parallel_hashmap",
        "lib/percy",
        "lib/rang",
    ],
    visibility = ["//visibility:public"],
)
