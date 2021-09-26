# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_binary(
    name = "lgshell",
    srcs = ["main.cpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [
        ":main",
    ],
)

cc_library(
    name = "main",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*test*.cpp", "main.cpp"],
    ) + glob([
        "*_api.hpp",
    ]),
    copts = COPTS,
    data = [
        "//inou/yosys:inou_yosys_scripts",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@replxx",
        "//core",
        "//elab",
        "//eprp",
        "//inou/cgen:inou_cgen",
        "//inou/code_gen:inou_code_gen",
        "//inou/firrtl:inou_firrtl_cpp",
        "//inou/graphviz:inou_graphviz",
        "//inou/json:inou_json",
        "//inou/liveparse:inou_liveparse",
        "//inou/pyrope:inou_pyrope",
        "//inou/prp:inou_prp",
        "//inou/slang:inou_slang",
        "//inou/yosys:lgyosys",
        "//lgcpp/common:lgcpp",
        "//lgcpp/prplib",
        "//pass/bitwidth:pass_bitwidth",
        "//pass/common:pass",
        "//pass/cprop:pass_cprop",
        "//pass/fplan",
        "//pass/label:pass_label",
        "//pass/lec:pass_lec",
        "//pass/lnast_fromlg:pass_lnast_fromlg",
        "//pass/lnast_tolg:pass_lnast_tolg",
        "//pass/lnastfmt:pass_lnastfmt",
        "//pass/lnastopt:pass_lnastopt",
        "//pass/punch:pass_punch",
        "//pass/sample:pass_sample",
        "//pass/sat_opt:pass_sat_opt",
        "//pass/semantic:pass_semantic",
        "//pass/submatch:pass_submatch",
        "//pass/compiler:pass_compiler",
        # "//pass/mockturtle:pass_mockturtle",

        #add dependencies to new passes here
    ],
)

cc_library(
    name = "tmt",
    srcs = ["tmt_test.cpp"],
    hdrs = ["tmt_test.hpp"],
    copts = COPTS,
)

cc_test(
    name = "main_test",
    srcs = ["main_test.cpp"],
    copts = COPTS,
    data = [
        ":lgshell",
    ],
    linkopts = ["-lutil"],
    local = 1,  # Weird fork error due to forkpty otherwise
    tags = [
        "fixme",
    ],
    deps = [
        ":tmt",
        "@com_google_googletest//:gtest_main",
        "@iassert",
    ],
)
