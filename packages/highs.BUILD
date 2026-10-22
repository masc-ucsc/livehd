# CPU-only standalone HiGHS library. Mirrors upstream 1.15.1's native target
# without its optional CUDA toolchain extension. Keep the upstream MIT license.
load("@bazel_skylib//rules:copy_file.bzl", "copy_file")
load("@rules_cc//cc:defs.bzl", "cc_library")

copy_file(
    name = "highs_config",
    src = "highs/HConfig.h.bazel.in",
    out = "HConfig.h",
)

cc_library(
    name = "highs",
    srcs = [
        "highs/interfaces/highs_c_api.cpp",
        "highs/HighsExternalApi.cpp",
        "highs/HighsExternalDeps.cpp",
        ":highs_config",
    ] + glob([
        "highs/io/*.cpp",
        "highs/io/filereaderlp/*.cpp",
        "highs/ipm/*.cpp",
        "highs/ipm/ipx/*.cc",
        "highs/ipm/basiclu/*.c",
        "highs/ipm/hipo/ipm/*.cpp",
        "highs/ipm/hipo/factorhighs/*.cpp",
        "highs/ipm/hipo/auxiliary/*.cpp",
        "highs/lp_data/*.cpp",
        "highs/mip/*.cpp",
        "highs/model/*.cpp",
        "highs/parallel/*.cpp",
        "highs/pdlp/*.cpp",
        "highs/pdlp/cupdlp/*.c",
        "highs/pdlp/hipdlp/*.cc",
        "highs/presolve/*.cpp",
        "highs/qpsolver/*.cpp",
        "highs/simplex/*.cpp",
        "highs/test_kkt/*.cpp",
        "highs/util/*.cpp",
    ]),
    hdrs = glob(["**/*.h", "**/*.hh", "**/*.hpp"]),
    defines = ["CUPDLP_CPU"],
    includes = [".", "highs", "extern"],
    linkopts = ["-lpthread"],
    visibility = ["//visibility:public"],
    deps = ["@zlib//:zlib"],
)
