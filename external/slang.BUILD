load("@rules_foreign_cc//foreign_cc:defs.bzl", "cmake")

filegroup(
    name = "all",
    srcs = glob(["**"]),
)

cmake(
    name = "slang",
    build_args = select({
        "@platforms//os:macos": [
            "--",
            "-j 4",
        ],
        "//conditions:default": [
        ],
    }),
    cache_entries = {
        "SLANG_INCLUDE_TESTS": "OFF",
        "SLANG_INCLUDE_TOOLS": "OFF",
        "CMAKE_POSITION_INDEPENDENT_CODE": "ON",
    },
    generate_args = select({
        "@platforms//os:macos": [
        ],
        "//conditions:default": [
            "-GNinja",
        ],
    }),
    lib_source = ":all",
    out_static_libs = [
        "libslangruntime.a",
        "libslangparser.a",
        "libslangcompiler.a",
        "libslangcore.a",
    ],
    visibility = ["//visibility:public"],
)
