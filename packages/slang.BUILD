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
        "SLANG_INCLUDE_TESTS": "OFF",
        "SLANG_INCLUDE_TOOLS": "OFF",
        "SLANG_INCLUDE_DOCS": "OFF",
        "SLANG_INCLUDE_PYLIB": "OFF",
        #"SLANG_INCLUDE_INSTALL": "OFF",
        "CMAKE_POSITION_INDEPENDENT_CODE": "ON",
        "CMAKE_BUILD_TYPE": "Release",
        # v11 defaults SLANG_USE_MIMALLOC ON; keep it OFF (mimalloc would
        # FetchContent, which the network-less Bazel sandbox cannot do). The dead
        # v10 SLANG_USE_BOOST cache entry is gone (slang.patch vendors boost).
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
    # slang.patch builds slang against the vendored single-header boost
    # (boost_unordered.hpp / boost_concurrent.hpp). Propagate the same define to
    # LiveHD so its slang-header compiles pick the vendored path, not <boost/...>.
    defines = [
        "SLANG_BOOST_SINGLE_HEADER",
    ],
    visibility = ["//visibility:public"],
    data = [
        "@fmt//:all",
        "@fmt//:fmt_cmake",
    ],
    deps = [
        "@fmt",
    ],
)
