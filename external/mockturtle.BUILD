# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "mockturtle",
    srcs = glob(["lib/abcsat/*.cpp"]),
    hdrs = glob(["include/**/*.hpp"]) + glob([
        "lib/ez/ez/*.hpp",
    ]) +
    #        + glob(["lib/bill/bill/sat/interface/*.hpp"])
    #        + glob(["lib/bill/bill/sat/solver/*.hpp"])
    #        + glob(["lib/bill/bill/sat/*.hpp"])
    #        + glob(["lib/bill/bill/utils/*.hpp"])
    glob([
        "lib/kitty/**/*.hpp",
    ]) + glob([
        "lib/percy/percy/*.h",
    ]) + glob([
        "lib/percy/percy/*.hpp",
    ]) + glob([
        "lib/percy/percy/solvers/*.hpp",
    ]) + glob([
        "lib/percy/percy/encoders/*.hpp",
    ]) + glob([
        "lib/abcsat/abc/*.h",
    ]) + glob([
        "lib/sparsepp/sparsepp/*.h",
    ]),
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
        "lib/abcsat",
        "lib/bill",
        "lib/ez",
        "lib/kitty",
        "lib/percy",
        "lib/sparsepp",
    ],
    visibility = ["//visibility:public"],
)
