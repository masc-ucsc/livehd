
load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")


cc_binary(
    name = "slang.so",        # Bazel does not enforce the prefix/suffix
    srcs = glob(
        ["src/*.cc"],
        exclude = ["*test*.cpp"],
    ),
    # Hidden visibility: the plugin loads into processes that also carry
    # slang v11 (lhd embeds the v11 direct reader), and any EXPORTED weak
    # slang v10 template/inline instantiation gets weak-coalesced by dyld
    # against the main executable's identically-mangled v11 copy (observed:
    # Driver::parseCommandLine resolving into lhd's v11 CommandLine ->
    # EXC_BAD_ACCESS). Plugin registration runs via static constructors, so
    # nothing needs exporting. The matching knob for the v10 archive itself
    # is in packages/slang_v10.BUILD (CMAKE_CXX_VISIBILITY_PRESET=hidden).
    copts = [
        "-fvisibility=hidden",
        "-fvisibility-inlines-hidden",
    ],
    visibility = ["//visibility:public"],
    deps = [ ":yosys_slang" ],
    linkshared = True,
    linkopts = select({
        "@platforms//os:macos": ["-Wl,-undefined,dynamic_lookup"],
        "//conditions:default": [
            "-Wl,-z,lazy",     # opposite of -z now
            "-Wl,-z,norelro",  # opposite of -z relro
        ],
    }),
    features = ["-hardening"],
    #features = ["pic"],
)

cc_library(
    name = "yosys_slang",
    hdrs = glob(["src/*.h"]),
    includes = ["src"],
    visibility = ["//visibility:public"],
    defines = ["YOSYS_ENABLE_PLUGINS", "SLANG_STATIC_DEFINE", "yosys_slang_EXPORTS"],
    deps = [
        # Pinned slang v10.0 (todo/ 1s): yosys-slang upstream is written against
        # the v10 slang API. The direct inou/slang reader uses @slang (v11.0).
        "@slang_v10//:slang",
        "@at_clifford_yosys2//:kernel_include",
        #"@at_clifford_yosys2//:version",
    ],

    #alwayslink = True,
)
