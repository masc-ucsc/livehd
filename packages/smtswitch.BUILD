# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# smt-switch (BSD-3) built from source via rules_foreign_cc cmake(), with the
# cvc5 backend pointed at the prebuilt @cvc5 static libs (smtswitch.patch
# rewrites cvc5/CMakeLists.txt's hard-coded build-tree paths to the release
# layout: ${CVC5_HOME}/lib/*.a + ${CVC5_HOME}/include). The cvc5 backend's
# static lib is repacked (ar -M) to bundle libcvc5.a + LibPoly + CaDiCaL, so
# downstream only needs libsmt-switch{,-cvc5}.a + dynamic GMP. See lec.md
# "Build sequence" step 2.

load("@rules_foreign_cc//foreign_cc:defs.bzl", "cmake")

filegroup(
    name = "all",
    srcs = glob(["**"]),
)

cmake(
    name = "smt_switch",
    cache_entries = {
        # Static library; CVC5_HOME points at the rules_foreign_cc deps dir
        # where @cvc5's headers (include/cvc5/*.h) and libs (lib/*.a) land.
        "SMT_SWITCH_LIB_TYPE": "STATIC",
        "BUILD_CVC5": "ON",
        # rules_foreign_cc converts `$$` -> `$`, so this yields the shell ref
        # `$EXT_BUILD_DEPS` (a trailing `$$` would leave a stray `$`).
        "CVC5_HOME": "$$EXT_BUILD_DEPS",
        "BUILD_TESTS": "OFF",
        "BUILD_PYTHON_BINDINGS": "OFF",
        "CMAKE_BUILD_TYPE": "Release",
        "CMAKE_POSITION_INDEPENDENT_CODE": "ON",
    },
    env = {
        "CCACHE_DISABLE": "1",
    },
    generate_args = ["-GNinja"],
    lib_source = ":all",
    out_static_libs = [
        "libsmt-switch.a",
        "libsmt-switch-cvc5.a",
    ],
    visibility = ["//visibility:public"],
    # Headers only: a static smt-switch-cvc5 compiles against cvc5 headers and
    # is linked against cvc5 downstream (@cvc5, combined+localized). Depending
    # on the full @cvc5 here would re-propagate the un-localized CaDiCaL.
    deps = ["@cvc5//:cvc5_headers"],
)
