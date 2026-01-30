
load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")


cc_binary(
    name = "slang.so",        # Bazel does not enforce the prefix/suffix
    srcs = glob(
        ["src/*.cc"],
        exclude = ["*test*.cpp"],
    ),
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
        "@slang",
        "@at_clifford_yosys2//:kernel_include",
        #"@at_clifford_yosys2//:version",
    ],
    #alwayslink = True,
)
