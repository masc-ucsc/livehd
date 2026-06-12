load("@rules_foreign_cc//foreign_cc:defs.bzl", "cmake")

# Pinned slang v10.0 for @yosys_slang only (todo/ 1s). The direct inou/slang
# reader moved to slang v11.0 (@slang); yosys-slang upstream (pinned at
# 7cdd147) is written against the v10 AST/numeric API (ConstantRange
# isLittleEndian, the pre-VisitFlags ASTVisitor, ...), so it keeps its own v10
# slang here instead of being re-ported. yosys_slang builds a separate
# slang.so plugin, so the two slang/fmt versions never link into one binary.

filegroup(
    name = "all",
    srcs = glob(["**"]),
)

cmake(
    name = "slang",
    generate_args = ["-GNinja"],
    cache_entries = {
        "FMT_CMAKE_PATH": "$(execpath @fmt_v10//:fmt_cmake)",
        "SLANG_INCLUDE_TESTS": "OFF",
        "SLANG_INCLUDE_TOOLS": "OFF",
        "SLANG_INCLUDE_DOCS": "OFF",
        "SLANG_INCLUDE_PYLIB": "OFF",
        #"SLANG_INCLUDE_INSTALL": "OFF",
        "CMAKE_POSITION_INDEPENDENT_CODE": "ON",
        "CMAKE_BUILD_TYPE": "Release",
        "SLANG_USE_BOOST": "OFF",
        "SLANG_USE_MIMALLOC": "OFF",
        # slang.so loads into processes that also carry slang v11 (lhd embeds
        # the v11 reader). With default visibility the v10 weak/inline
        # template instantiations (e.g. Driver::parseCommandLine<char**>) are
        # exported and dyld's cross-image weak coalescing binds them to the
        # MAIN executable's v11 copies -> EXC_BAD_ACCESS on v10 objects.
        # Hidden visibility keeps every v10 symbol module-local to the plugin.
        "CMAKE_CXX_VISIBILITY_PRESET": "hidden",
        "CMAKE_VISIBILITY_INLINES_HIDDEN": "ON",
    },
    lib_source = ":all",
    # slang's CMakeLists.txt force-enables ccache as the compiler launcher when
    # it finds one; ccache then fails writing to its read-only $HOME/.ccache dir
    # in the foreign_cc sandbox. CCACHE_DISABLE makes ccache a passthrough.
    env = {
        "CCACHE_DISABLE": "1",
    },
    out_static_libs = [
        "libsvlang.a",
    ],
    visibility = ["//visibility:public"],
    data = [
        "@fmt_v10//:all",
        "@fmt_v10//:fmt_cmake",
    ],
    deps = [
        "@fmt_v10//:fmt",
        "@boost.unordered",
        "@boost.multiprecision",
    ],
)
