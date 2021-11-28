# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "cryptominisat",
    srcs = glob(
        [
            "src/*.cpp",
            "src/*.c",
        ],
        exclude = [
            "src/toplevelgauss.cpp",
            "src/main*.cpp",
            "src/watcharray_*.cpp",
            "src/fuzz*.cpp",
            "src/sqlitestats.cpp",
            "src/gatefinder.cpp",
            "src/cl_predictors.cpp",
            "src/cms_bosphorus.cpp",  # No MIT
            "src/datasyncserver.cpp",  # No MPI
            "src/cms_breakid.cpp",  # No breakid compile option
        ],
    ),
    hdrs = glob(
        [
            "src/*.h",
            "include/cryptominisat5/*.h",
        ],
    ),
    copts = [
        "-w",
        "-O2",
    ],
    defines = [
        "USE_GAUSS",
        "cryptominisat5_EXPORTS",
    ],
    includes = [
        "include",
    ],
    linkopts = ["-lpthread"],
    visibility = ["//visibility:public"],
)
