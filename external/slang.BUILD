load("@rules_foreign_cc//foreign_cc:defs.bzl", "cmake")

filegroup(
    name = "all",
    srcs = glob(["**"]),
)

cmake(
    name = "slang",
    cache_entries = {
        "SLANG_INCLUDE_TESTS": "OFF",
        "SLANG_INCLUDE_TOOLS": "OFF",
        "CMAKE_POSITION_INDEPENDENT_CODE": "ON",
    },
    generate_args = [
        "-GNinja",
    ],
    lib_source = ":all",
    out_static_libs = [
        "libslangruntime.a",
        "libslangparser.a",
        "libslangcompiler.a",
        "libslangcore.a",
    ],
    visibility = ["//visibility:public"],
)
