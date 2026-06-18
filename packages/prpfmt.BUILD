# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# prpfmt — the Pyrope source formatter (a clang-format for Pyrope) that ships
# in the tree-sitter-pyrope repo (prpfmt/). It walks a tree-sitter parse of the
# generated grammar (src/parser.c + src/scanner.c) and re-emits standardized
# Pyrope. LiveHD embeds it as `lhd pyrope fmt` via the C entry point
# prpfmt_format_string (prpfmt/prpfmt_api.h).
#
# This overlay roots the external repo at the tree-sitter-pyrope repo *root*
# (so src/ and prpfmt/ are both reachable) — distinct from @prpparse, which
# roots at the prpparse/ subdir. External dep -> warnings off (the formatter's
# deliberately out-of-range obsolete-node enum ids trip -Wswitch).
load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "prpfmt",
    srcs = [
        "prpfmt/ir.c",
        "prpfmt/prpfmt.c",
        "src/parser.c",
        "src/scanner.c",
        # internal headers (resolved same-tree via the includes below)
        "prpfmt/ir.h",
        "prpfmt/prpfmt.h",
        "prpfmt/ts_symbols.h",
        # the generated parser's build headers (parser.h / array.h / alloc.h)
    ] + glob(["src/tree_sitter/*.h"]),
    # The only header an embedder needs; pulls in no tree-sitter types.
    hdrs = ["prpfmt/prpfmt_api.h"],
    copts = [
        "-std=gnu11",  # open_memstream / clock_gettime visibility on glibc
        "-w",
    ],
    # src/  -> the generated parser's "tree_sitter/parser.h"
    # prpfmt/ -> prpfmt.h / ir.h / ts_symbols.h / prpfmt_api.h
    includes = [
        "prpfmt",
        "src",
    ],
    visibility = ["//visibility:public"],
    # <tree_sitter/api.h> — the parsing runtime.
    deps = ["@tree_sitter"],
)
