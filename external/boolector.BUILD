# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "boolector",
    srcs = glob(
        ["src/*.c"],
        exclude = [
            "src/boolectormain.c",
            "src/btormbt.c",
            "src/btormcmain.c",
            "src/btoruntrace.c",
            "src/btorparse.c",
            "src/btormain.c",
        ],
    ) + glob([
        "src/sat/*.c",
    ]) + glob([
        "src/utils/*.c",
        "src/preprocess/*.c",
        "src/dumper/*.c",
    ]) + ["src/sat/btorcms.cc"],
    hdrs = glob([
        "src/*.h",
        "src/utils/*.h",
        "src/preprocess/*.h",
        "src/sat/*.h",
        "src/dumper/*.h",
    ]),
    copts = [
        "-w",
        "-O2",
    ],
    defines = ["BTOR_USE_CMS=1"],
    includes = ["src"],
    visibility = ["//visibility:public"],
    deps = [
        "@cryptominisat",
    ],
)
