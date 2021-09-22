load("@rules_foreign_cc//tools/build_defs:cmake.bzl", "cmake_external")

filegroup(
    name = "all",
    srcs = glob(["**"]),
)

cmake_external(
    name = "slang",
    cmake_options = [
        "-GNinja",  # remember to install ninja in OS X
        "-DSLANG_INCLUDE_TESTS=OFF",
        "-DSLANG_INCLUDE_TOOLS=OFF",
        "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
    ],
    lib_source = ":all",
    make_commands = [
        "ninja",
        "ninja install",
    ],
    static_libraries = [
        "libslangruntime.a",
        "libslangparser.a",
        "libslangcompiler.a",
        "libslangcore.a",
    ],
    visibility = ["//visibility:public"],
)
