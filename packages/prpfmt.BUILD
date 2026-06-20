# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# prpfmt — the Pyrope source formatter (a clang-format for Pyrope) that ships
# in the tree-sitter-pyrope repo (prpfmt/). It walks a tree-sitter parse of the
# generated grammar (src/parser.c + src/scanner.c) and re-emits standardized
# Pyrope. The formatter is C++ as of upstream ee58755 (prpfmt/ir.cc + prpfmt.cc;
# was C); the generated parser/scanner remain C. LiveHD embeds it as
# `lhd pyrope fmt` via the stable C-ABI entry point prpfmt_format_string
# (prpfmt/prpfmt_api.h, extern "C").
#
# This overlay roots the external repo at the tree-sitter-pyrope repo *root*
# (so src/ and prpfmt/ are both reachable) — distinct from @prpparse, which
# roots at the prpparse/ subdir. External dep -> warnings off (the formatter's
# deliberately out-of-range obsolete-node enum ids trip -Wswitch).
load("@rules_cc//cc:defs.bzl", "cc_library")

# prpfmt/ts_symbols.h is generated (gitignored upstream, so absent from the
# pinned archive). It lifts the authoritative `enum ts_symbol_identifiers` out of
# the generated src/parser.c — the ids get renumbered on every grammar change, so
# this must be regenerated rather than vendored. Mirrors prpfmt/gen_symbols.sh.
genrule(
    name = "gen_ts_symbols",
    srcs = ["src/parser.c"],
    outs = ["prpfmt/ts_symbols.h"],
    cmd = "\n".join([
        "{",
        "  echo '/* AUTO-GENERATED from src/parser.c by gen_symbols.sh -- DO NOT EDIT. */'",
        "  echo '#ifndef PRPFMT_TS_SYMBOLS_H'",
        "  echo '#define PRPFMT_TS_SYMBOLS_H'",
        "  sed -n '/^enum ts_symbol_identifiers {/,/^};/p' $(location src/parser.c)",
        "  echo '#endif /* PRPFMT_TS_SYMBOLS_H */'",
        "} > $@",
    ]),
)

cc_library(
    name = "prpfmt",
    srcs = [
        # The formatter is C++ as of upstream ee58755 (was C); the generated
        # tree-sitter parser/scanner stay C and are compiled/linked as C.
        "prpfmt/ir.cc",
        "prpfmt/prpfmt.cc",
        "src/parser.c",
        "src/scanner.c",
        # internal headers (resolved same-tree via the includes below)
        "prpfmt/ir.h",
        "prpfmt/prpfmt.h",
        ":gen_ts_symbols",  # generated prpfmt/ts_symbols.h
        # the generated parser's build headers (parser.h / array.h / alloc.h)
    ] + glob(["src/tree_sitter/*.h"]),
    # The only header an embedder needs; pulls in no tree-sitter types.
    hdrs = ["prpfmt/prpfmt_api.h"],
    # The .c files get -std=gnu17 and the .cc files -std=c++23 from .bazelrc's
    # global BAZEL_CONLYOPTS / --cxxopt; do NOT pin -std here (copts apply to both
    # languages and one std would reject the other). -std=c++23 is strict-ANSI,
    # which hides POSIX open_memstream / clock_gettime on glibc, so re-expose them
    # via _GNU_SOURCE (harmless for the C TUs, which already have it under gnu17).
    copts = [
        "-w",
        "-D_GNU_SOURCE",
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
