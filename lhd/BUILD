# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

# lhd — the stateless, hermetic LiveHD CLI kernel (task 1y-bazel).
# Contract: docs/contracts/future_cli.md. The lgshell REPL (//main:lgshell)
# is untouched; lhd links the same pass/inou registry and drives it via argv.

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "lhd_lib",
    srcs = [
        "lhd_kernel.cpp",
        "lhd_meta.cpp",
        "lhd_options.cpp",
        "lhd_result.cpp",
    ],
    hdrs = ["lhd.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//core",
        "//graph",
        "//inou/cgen:inou_cgen",
        "//inou/prp:inou_prp",
        "//inou/slang:inou_slang",
        "//inou/yosys:lgyosys",
        "//lnast",
        "//pass/bitwidth:pass_bitwidth",
        "//pass/common:pass",
        "//pass/cprop:pass_cprop",
        "//pass/lnastfmt:pass_lnastfmt",
        "//pass/prp_writer:pass_prp_writer",
        "//pass/upass:pass_upass",
        "//upass/tolg:upass_tolg",
        "@rapidjson",
    ],
)

cc_binary(
    name = "lhd",
    srcs = ["lhd_main.cpp"],
    copts = COPTS,
    data = [
        "//inou/yosys:scripts",
    ],
    linkopts = select({
        "@platforms//os:macos": ["-lz"],
        "//conditions:default": ["-Wl,--export-dynamic", "-lz"],
    }),
    visibility = ["//visibility:public"],
    deps = [
        ":lhd_lib",
        "//lsp",
    ],
)
