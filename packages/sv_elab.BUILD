load("@rules_cc//cc:defs.bzl", "cc_library")

genrule(
    name = "version",
    outs = ["src/version.h"],
    cmd = "printf '#pragma once\n#define YOSYS_SLANG_REVISION \"b6e440d6\"\n#define SLANG_REVISION \"11.0\"\n' > $@",
)

cc_library(
    name = "frontend",
    srcs = glob(["src/*.cc", "src/yosys_plugin/*.cc"]),
    hdrs = glob(["src/*.h", "src/yosys_plugin/*.h", "third_party/hashlib/*.h"]) + [":version"],
    includes = ["src", "src/yosys_plugin", "third_party/hashlib"],
    defines = ["SLANG_STATIC_DEFINE", "YOSYS_MAJOR=0", "YOSYS_MINOR=68"],
    deps = ["@slang//:slang", "@at_clifford_yosys2//:kernel_include"],
    alwayslink = True,
    visibility = ["//visibility:public"],
)
