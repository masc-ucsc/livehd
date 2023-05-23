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
            "-j 8",
        ],
        "//conditions:default": [
        ],
    }),
    cache_entries = {
        "SLANG_INCLUDE_TESTS": "OFF",
        "SLANG_INCLUDE_TOOLS": "OFF",
        "SLANG_INCLUDE_DOCS": "OFF",
        "SLANG_INCLUDE_PYLIB": "OFF",
        #"SLANG_INCLUDE_INSTALL": "OFF",
        "CMAKE_POSITION_INDEPENDENT_CODE": "ON",
        "CMAKE_BUILD_TYPE": "Release",
         "SLANG_USE_BOOST": "OFF",
         "SLANG_USE_MIMALLOC": "OFF",
    },
#    generate_args = select({
#        "@platforms//os:macos": [
#        ],
#        "//conditions:default": [
#            "-GNinja",
#        ],
#    }),
    lib_source = ":all",
    out_static_libs = [
        "libsvlang.a",
    ],
    visibility = ["//visibility:public"],
    data = [
        "@mimalloc//:all",
        "@fmt//:all",
    ],
    deps = [
        "@fmt",
    ]
)
