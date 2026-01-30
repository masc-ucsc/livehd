# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

#config_setting(
#    name = "use_tcmalloc",
#    values = {"define": "tcmalloc=1"},
#)

cc_binary(
    name = "lgshell",
    srcs = ["main.cpp"],
    copts = COPTS,
    malloc = select({
        #        ":use_tcmalloc": "@com_google_tcmalloc//tcmalloc",
        "//conditions:default": "@bazel_tools//tools/cpp:malloc",
    }),
    linkopts = select({
        "@platforms//os:macos": ["-lz"],
        "//conditions:default": ["-Wl,--export-dynamic", "-lz"],
    }),
    visibility = ["//visibility:public"],
    deps = [
        ":main",
    ],
)

cc_library(
    name = "main",
    srcs = glob(
        ["*.cpp"],
        exclude = [
            "*test*.cpp",
            "main.cpp",
        ],
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
        "//lnast",
        "//lgraph",
        "//inou/cgen:inou_cgen",
        "//inou/code_gen:inou_code_gen",
        "//inou/attr:inou_attr",
        "//inou/locator:traverse_lg",
        "//inou/graphviz:inou_graphviz",
        "//inou/json:inou_json",
        "//inou/lefdef:inou_lefdef",
        "//inou/liberty:inou_liberty",
        "//inou/liveparse:inou_liveparse",
        "//inou/pyrope:inou_pyrope",
        "//inou/prp:inou_prp",
        "//inou/slang:inou_slang",
        "//inou/yosys:lgyosys",
        "//inou/lnast:inou_lnast",
        "//lgext/common:lgcpp",
        "//lgext/prplib",
        "//pass/bitwidth:pass_bitwidth",
        "//pass/common:pass",
        "//pass/cprop:pass_cprop",
        "//pass/label:pass_label",
        "//pass/lnast_fromlg:pass_lnast_fromlg",
        "//pass/lnast_tolg:pass_lnast_tolg",
        "//pass/lnastfmt:pass_lnastfmt",
        "//pass/locator:pass_locator",
        "//pass/lnastopt:pass_lnastopt",
        "//pass/opentimer:pass_opentimer",
        "//pass/punch:pass_punch",
        "//pass/sample:pass_sample",
        "//pass/semantic:pass_semantic",
        "//pass/submatch:pass_submatch",
        "//pass/compiler:pass_compiler",
        "//pass/randomize_dpins:pass_randomize_dpins",
        # "//pass/mockturtle:pass_mockturtle",
        #add dependencies to new passes here
        #"//inou/hif:inou_hif"
        "//pass/upass:pass_upass",
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
        "@googletest//:gtest_main",
        "@iassert",
    ],
)
