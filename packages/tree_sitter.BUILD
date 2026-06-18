# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# tree-sitter — the incremental-parsing C runtime (libtree-sitter). LiveHD's
# Pyrope front-end is the hand-written prpparse (no tree-sitter), but the
# `lhd pyrope fmt` formatter (@tree_sitter_pyrope//:prpfmt, the prpfmt tool)
# walks a tree-sitter parse, so it links this runtime.
#
# The whole runtime builds from the single amalgamation TU lib/src/lib.c, which
# #includes every other runtime .c. The WASM engine (wasm_store.c) is guarded
# behind TREE_SITTER_FEATURE_WASM (left off here — the formatter never uses it),
# so no wasmtime dependency is pulled in. External dep -> warnings off.
load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "tree_sitter",
    srcs = ["lib/src/lib.c"],
    # lib.c textually includes the sibling .c files and the private headers
    # (alloc.h, array.h, parser.h, unicode/*, portable/endian.h, …); list them
    # so Bazel sandboxes them into the compile.
    textual_hdrs = glob(
        [
            "lib/src/*.c",
            "lib/src/*.h",
            "lib/src/unicode/*.h",
            "lib/src/portable/*.h",
        ],
        exclude = ["lib/src/lib.c"],
    ),
    hdrs = glob(["lib/include/tree_sitter/*.h"]),
    copts = ["-std=gnu11", "-w"],
    # Private headers resolve same-tree via -Ilib/src; the public api.h reaches
    # this runtime's own TU the same way consumers see it.
    includes = ["lib/src", "lib/include"],
    # Consumers (the prpfmt lib) only need <tree_sitter/api.h>.
    visibility = ["//visibility:public"],
)
