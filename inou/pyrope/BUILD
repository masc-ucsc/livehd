# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")
load("@rules_shell//shell:sh_test.bzl", "sh_test")
load("//tools:copt_default.bzl", "COPTS")

PYROPE_TESTS = glob([
    "tests/*.prp",  # Pyrope tests
    "tests/verilog_gld/*.v",  # Verilog golden models
])

cc_library(
    name = "inou_pyrope",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*test*.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//pass/common:pass",
    ],
    alwayslink = True,
)

filegroup(
    name = "pyrope_tests",
    srcs = glob(["tests/*.prp"]),
    visibility = ["//visibility:public"],
)

_PYROPE_PRP_BASES = {
    f[len("tests/"):-len(".prp")]: True
    for f in glob(["tests/*.prp"])
}

_PYROPE_COMPILE_SKIP = {
    # These have golden Verilog files, but the legacy inou/pyrope compile
    # harness still uses the raw lnast_tolg/cprop/bitwidth pipeline. Keep
    # work-in-progress cases out of //... until the lowering path supports them.
    "connect_through": True,
    "connect_through2": True,
    "firrtl_gcd": True,
    "funcall": True,
    "funcall3": True,
    "funcall4": True,
    "struct_uflop": True,
    "top": True,
    "tuple_nested2": True,
    "tuple_reg": True,
    "vec_shift_register_param": True,
}

PYROPE_COMPILE_TESTS = [
    base
    for base in [
        f[len("tests/verilog_gld/"):-len(".gld.v")]
        for f in glob(["tests/verilog_gld/*.gld.v"])
    ]
    if base in _PYROPE_PRP_BASES and base not in _PYROPE_COMPILE_SKIP
]

[sh_test(
    name = "pyrope_compile-%s" % t,
    srcs = ["tests/pyrope_compile.sh"],
    args = ["$(location //inou/pyrope:tests/%s.prp)" % t],
    data = [
        "//inou/pyrope:tests/%s.prp" % t,
        "//inou/pyrope:tests/verilog_gld/%s.gld.v" % t,
        "//inou/yosys:scripts",
        "//main:lgshell",
    ],
    tags = [
        "pyrope_compile",
    ],
    deps = [
        "//inou/yosys:scripts",
    ],
) for t in PYROPE_COMPILE_TESTS]

cc_binary(
    name = "prp_test",
    srcs = ["tests/prp_test.cpp"],
    copts = COPTS,
    deps = [
        ":inou_pyrope",
        "@googletest//:gtest_main",
    ],
)

cc_binary(
    name = "prp_lnast_test",
    srcs = ["tests/prp_lnast_test.cpp"],
    copts = COPTS,
    deps = [
        ":inou_pyrope",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "prp_unit_test",
    srcs = ["tests/prp_unit_test.cpp"],
    tags = [
        "fixme",
    ],
    deps = [
        ":inou_pyrope",
        "@googletest//:gtest_main",
    ],
)

exports_files(PYROPE_TESTS)
