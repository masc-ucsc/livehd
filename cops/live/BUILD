#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

cc_library(
    name = "cops_live",
    srcs = glob(["*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
        "//meta:meta",
        "//eprp:eprp",
    ],
)

cc_binary(
    name = "invariant_stats",
    srcs = glob(["tests/invariant_stats.cpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
        ":cops_live",
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

sh_test(
    name = "invariant-long",
    srcs = ["tests/invariant.sh"],
    deps = [
        ":scripts",
    ],
    tags = ["long1"],
    size = "large",
    args = ["long"],
    data = [
        "//inou/yosys:liblgraph_yosys.so",
        "//inou/tech:tech_gens",
        "//main:lgshell",
        "@anubis//:anubis",
    ],
)
