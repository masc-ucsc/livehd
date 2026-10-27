load("@rules_foreign_cc//foreign_cc:defs.bzl", "cmake")

filegroup(
    name = "all",
    srcs = glob(["**"]),
)

cmake(
    name = "slang",
    generate_args = ["-GNinja"],
    cache_entries = {
        "FMT_CMAKE_PATH": "$(execpath @fmt//:fmt_cmake)",
        "BOOST_REGEX_CMAKE_PATH": "$(execpath @slang_regex//:cmake_root)",
        "TOMLPLUSPLUS_CMAKE_PATH": "$(execpath @slang_toml//:cmake_root)",
        "SLANG_INCLUDE_TESTS": "OFF",
        "SLANG_INCLUDE_TOOLS": "OFF",
        "SLANG_INCLUDE_DOCS": "OFF",
        "SLANG_INCLUDE_PYLIB": "OFF",
        #"SLANG_INCLUDE_INSTALL": "OFF",
        "CMAKE_POSITION_INDEPENDENT_CODE": "ON",
        "CMAKE_BUILD_TYPE": "Release",
        # FetchContent dependencies come from Bazel archives, never the host.
        "SLANG_USE_MIMALLOC": "OFF",
        "SLANG_USE_SYSTEM_FMT": "OFF",
        "SLANG_USE_SYSTEM_BOOST": "OFF",
        "SLANG_USE_SYSTEM_TOMLPLUSPLUS": "OFF",
    },
#    generate_args = select({
#        "@platforms//os:macos": [
#        ],
#        "//conditions:default": [
#            "-GNinja",
#        ],
#    }),
    lib_source = ":all",
    # slang's CMakeLists.txt force-enables ccache as the compiler launcher when it
    # finds one; ccache then fails writing to its $HOME/.ccache dir (read-only in
    # the foreign_cc sandbox). CCACHE_DISABLE makes ccache a passthrough so the
    # compile uses the toolchain directly. Scoped here -- no global cache impact.
    env = {
        "CCACHE_DISABLE": "1",
    },
    out_static_libs = [
        "libsvlang.a",
    ],
    visibility = ["//visibility:public"],
    data = [
        "@fmt//:all",
        "@fmt//:fmt_cmake",
        "@slang_regex//:all",
        "@slang_regex//:cmake_root",
        "@slang_toml//:all",
        "@slang_toml//:cmake_root",
    ],
    deps = [
        "@fmt",
    ],
)
