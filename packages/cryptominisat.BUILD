# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "cryptominisat",
    srcs = glob(
        [
            "src/*.cpp",
            "src/*.c",
            "src/oracle/*.cpp",
            "src/picosat/*.c",
        ],
        exclude = [
            "src/toplevelgauss.cpp",
            "src/main*.cpp",
            "src/watcharray_*.cpp",
            "src/fuzz*.cpp",
            "src/sqlitestats.cpp",
            "src/cl_predictors_abs.cpp",
            "src/cl_predictors_lgbm.cpp",
            "src/cl_predictors_py.cpp",
            "src/cl_predictors_xgb.cpp",
            "src/community_finder.cpp",
            "src/cms_bosphorus.cpp",  # No MIT
            "src/datasyncserver.cpp",  # No MPI
            "src/cms_breakid.cpp",  # No breakid compile option
            "src/picosat/main.c",
            "src/picosat/picomcs.c",
            "src/picosat/picomus.c",
            "src/picosat/picogcnf.c",
        ],
    ),
    deps = [
      # No need to have boost
      #      "@boost//:serialization",
    ],
    hdrs = glob(
        [
            "src/*.h",
            "src/oracle/*.h",
            "src/picosat/*.h",
            "include/cryptominisat5/*.h",
        ],
    ),
    copts = [
        "-w",
        "-O2",
    ],
    defines = [
        "USE_GAUSS",
        "YALSAT_FPU",
        "TRACE", # needed for PicoSAT
        "cryptominisat5_EXPORTS",
    ],
    includes = [
        "include",
        "include/cryptominisat5",
    ],
    linkopts = ["-lpthread"],
    visibility = ["//visibility:public"],
)
