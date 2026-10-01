# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("@rules_shell//shell:sh_test.bzl", "sh_test")
load("//tools:copt_default.bzl", "COPTS")

# Reusable engine: structural_match(a, b) stamps the `match` attribute. Free of
# the cvc5-heavy pass/lec dep (the name normalization is replicated locally), so
# `lec` can call it cheaply to pre-match before building its miter (v2).
cc_library(
    name = "semdiff",
    srcs = ["semdiff.cpp"],
    hdrs = ["semdiff.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//core",
        "//graph",
    ],
)

# Plugin: pass.semdiff (the semdiff.* set-option registry + pass entry point).
cc_library(
    name = "pass_semdiff",
    srcs = ["pass_semdiff.cpp"],
    hdrs = ["pass_semdiff.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        ":semdiff",
        "//core",
        "//pass/common:pass",
    ],
    alwayslink = True,  # static Pass_plugin registration
)

cc_test(
    name = "semdiff_test",
    srcs = ["semdiff_test.cpp"],
    copts = COPTS,
    deps = [
        ":semdiff",
        "//core",
        "//graph",
        "@googletest//:gtest_main",
    ],
)

# lec's no-solver structural skip against a bus_name-reconstructed state pair
# (ref `g` vs impl `DFF g`/`\g.l`/`\g_cgen1.l`): swapped dins or a changed
# reset/initial constant must REFUTE, the equal twins must still PROVE fast.
sh_test(
    name = "lec_name_hint_test",
    srcs = ["tests/lec_name_hint_test.sh"],
    data = ["//lhd"],
    tags = ["no-sandbox"],  # invokes the in-process cvc5 engines
)

# Hashed into the formal verdict-cache salt (see //pass/lec:formal_srcs): the
# canonical digest is computed here, so digest-affecting changes must
# invalidate cached verdicts too.
filegroup(
    name = "formal_srcs",
    srcs = glob([
        "*.cpp",
        "*.hpp",
    ]),
    visibility = ["//visibility:public"],
)
