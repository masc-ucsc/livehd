#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

cc_library(
    name = "cops_live",
    srcs = glob(["*.cpp"],
                exclude = [
                    "lginvariant.cpp",
                    "lglive.cpp",
                    "lgstitch.cpp",
                ],),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
        "//meta:meta",
    ],
)

sh_library(
    name = "scripts",
    srcs = [
        "lgsetup",
        "lglivesynth",
    ],
    visibility = ["//visibility:public"],
)

sh_test(
    name = "invariant",
    srcs = ["tests/invariant.sh"],
    deps = [
        ":scripts",
    ],
    data = [
        "//inou/yosys:liblgraph_yosys.so",
        "//inou/tech:tech_gens",
        "//main:lgshell",
        "@anubis//:anubis",
    ],
)
