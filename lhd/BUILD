# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

# lhd — the stateless, hermetic LiveHD CLI kernel (task 1y-bazel).
# Contract: the LiveHD docs. lhd links the pass/inou registry
# and drives it via argv; the legacy lgshell REPL was removed 2026-06-04.

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
        "//pass/abc:pass_abc",
        "//pass/bitwidth:pass_bitwidth",
        "//pass/color:pass_color",
        "//pass/common:pass",
        "//pass/liberty:pass_liberty",
        "//pass/cprop:pass_cprop",
        "//pass/isabelle:pass_isabelle",
        "//pass/partition:pass_partition",
        "//pass/lnastfmt:pass_lnastfmt",
        "//pass/prp_writer:pass_prp_writer",
        "//pass/upass:pass_upass",
        "//upass/tolg:upass_tolg",
        "@hhds//hhds:tree_edit_distance",
        "@rapidjson",
    ],
)

cc_binary(
    name = "lhd",
    srcs = ["lhd_main.cpp"],
    additional_linker_inputs = ["lhd_export.lds"],
    copts = COPTS,
    data = [
        "//inou/yosys:scripts",
    ],
    # `--export-dynamic` lets the dlopen'd yosys-slang plugin bind to the Yosys
    # symbols in this binary; the version script then force-localizes lhd's own
    # slang/fmt/boost so they do not interpose the plugin's incompatible
    # embedded copies (see lhd_export.lds). macOS has no version-script flag and
    # does not run the yosys-slang plugin path, so it is Linux-only.
    linkopts = select({
        "@platforms//os:macos": ["-lz"],
        "//conditions:default": [
            "-Wl,--export-dynamic",
            "-lz",
            "-Wl,--version-script=$(location lhd_export.lds)",
        ],
    }),
    visibility = ["//visibility:public"],
    deps = [
        ":lhd_lib",
        "//lsp",
    ],
)
