# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# prpparse — the hand-written recursive-descent Pyrope parser (lives at
# ../tree-sitter-pyrope/prpparse) that replaces the generated tree-sitter parser
# as the LiveHD front-end (see prpparse/plan.md). This overlay BUILD roots the
# external repo at the prpparse/ directory, so its bare internal includes
# (`#include "ast.hpp"`) resolve same-directory while consumers access the
# headers under a `prpparse/` prefix — avoiding a clash with LiveHD's own
# same-named core headers (diag.hpp / parser.hpp). External dep -> warnings off.
load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "prpparse",
    srcs = glob(
        ["*.cpp"],
        exclude = ["tests/*.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    textual_hdrs = glob(["*.def"]),
    # Headers reach consumers as `prpparse/<hdr>`. prpparse's own bare internal
    # includes keep working via same-directory quote resolution. (prpparse's
    # `diag.hpp` was renamed `prp_diag.hpp` so the repo-root -iquote bazel adds
    # for this dep cannot shadow LiveHD's bare `#include "diag.hpp"`.)
    include_prefix = "prpparse",
    # Mirror upstream prpparse/BUILD's buffer/stack hardening (ee58755). Orthogonal
    # to -w (warnings stay off) and cheap at any -O: _GLIBCXX_ASSERTIONS bounds-
    # checks std::vector/string_view/array (the lexer/token cursor use hand-rolled
    # `< n` guards), -fstack-protector-strong catches stack overruns. (Upstream's
    # third flag -D_FORTIFY_SOURCE=2 needs an optimized build, so it is left to -c opt.)
    copts = [
        "-std=c++23",
        "-w",
        "-D_GLIBCXX_ASSERTIONS",
        "-fstack-protector-strong",
    ],
    visibility = ["//visibility:public"],
    deps = ["@hhds//hhds:core"],
)
