# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Pono (BSD-3) model checker, built directly with Bazel (like abc/yosys) rather
# than its own CMake. We compile only the library core -- core/engines/smt/
# utils/options/modifiers/refiners -- against the prebuilt @smtswitch (cvc5
# backend). Pono's CMake hard-requires bitwuzla + Btor2Tools + flex/bison, but
# all of that lives in the file frontends (btor2/smv/vmt/coreir parsers) and the
# witness printers, which pass/lec does not use: it builds the
# TransitionSystem in memory and reads witnesses via smt-switch directly.
#
# Excluded from the build:
#   - frontends/        : btor2/smv/vmt/coreir file readers (need flex/bison +
#                         Btor2Tools); we encode LGraph -> TS ourselves.
#   - printers/         : btor2/vcd witness printers (pull frontends/).
#   - engines/msat_ic3ia.cpp : needs MathSAT (non-redistributable; off-limits).
# pono.patch gates the bitwuzla references in smt/available_solvers.cpp behind
# WITH_BITWUZLA (undefined here) so the cvc5-only build links. See lec.md
# "Build sequence" step 3 + "Build & dependency notes" (MathSAT/IC3IA excluded).

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "pono",
    srcs = glob(
        [
            "core/*.cpp",
            "engines/*.cpp",
            "smt/*.cpp",
            "utils/*.cpp",
            "options/*.cpp",
            "modifiers/*.cpp",
            "refiners/*.cpp",
        ],
        exclude = [
            "engines/msat_ic3ia.cpp",  # needs MathSAT
        ],
    ),
    hdrs = glob([
        "core/*.h",
        "engines/*.h",
        "smt/*.h",
        "utils/*.h",
        "options/*.h",
        "modifiers/*.h",
        "refiners/*.h",
        "contrib/optionparser-1.7/src/*.h",  # vendored Lean Mean C++ Option Parser
    ]),
    # External dep: suppress its warnings (matches packages/boolector.BUILD).
    # Pono is written to C++17. Several TUs rely on transitive <cassert> /
    # <cstdint> that this libstdc++ no longer pulls in -- force-include them.
    copts = [
        "-w",
        "-std=c++17",
        "-include",
        "cassert",
        "-include",
        "cstdint",
    ],
    includes = [
        ".",
        "contrib/optionparser-1.7/src",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@cvc5",  # combined+localized cvc5 (resolves smt-switch-cvc5's cvc5 refs)
        "@smtswitch//:smt_switch",
    ],
)
