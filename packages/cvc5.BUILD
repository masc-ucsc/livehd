# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# cvc5 prebuilt non-GPL static libraries (cvc5-<os>-<cpu>-static.zip from the
# official cvc5 releases). We consume the prebuilt archive rather than building
# cvc5 from source. This is the pass/lec SMT backend (smt-switch -> cvc5),
# covering QF_ABV and every Pono engine.
#
# LICENSING: keep the default (no-gpl) artifact -- never `-static-gpl.zip`. The
# bundled libgmp.a / libgmpxx.a are LGPLv3, so we deliberately do NOT import
# them; cvc5's GMP symbols are satisfied by the *system shared* libgmp/libgmpxx
# (the -lgmp/-lgmpxx linkopts below). See pass/lec/lec.md "Build & dependency
# notes".
#
# SYMBOL ISOLATION: cvc5 and Berkeley-abc (linked into the same `lhd` binary via
# the Yosys flow) BOTH vendor CaDiCaL, with incompatible copies -> duplicate
# strong symbols at the final link. We merge cvc5's static deps into one
# relocatable object and localize the CaDiCaL symbols, so cvc5 keeps a private
# CaDiCaL and abc keeps its own. (Mirrors lhd_export.lds, which localizes
# slang/fmt/boost for the same reason.)

load("@rules_cc//cc:defs.bzl", "cc_import", "cc_library")

# Headers only -- smt-switch's build-time dep. A static smt-switch-cvc5 only
# needs cvc5 *headers* to compile; pushing libs here would re-propagate the
# un-localized CaDiCaL and reintroduce the clash.
cc_library(
    name = "cvc5_headers",
    hdrs = glob(["include/**/*.h"]),
    includes = ["include"],
    visibility = ["//visibility:public"],
)

# Combine libcvc5 + parser + CaDiCaL + LibPoly into one relocatable object,
# then localize CaDiCaL (it is self-contained after `ld -r`, so its internal
# references stay bound while the symbols become file-local / non-exported).
genrule(
    name = "cvc5_combined",
    # No libcvc5parser.a: the C++-API backend never uses the SMT-LIB parser,
    # and it shares member basenames with libcvc5.a (which would collide).
    srcs = [
        "lib/libcvc5.a",
        "lib/libcadical.a",
        "lib/libpicpoly.a",
        "lib/libpicpolyxx.a",
    ],
    outs = ["libcvc5_combined.a"],
    # `ld -r --whole-archive` pulls EVERY member straight from the archives --
    # no `ar x` extraction (cvc5's archives have duplicate member basenames that
    # extraction would silently overwrite, dropping objects).
    cmd = """
set -e
work=$$(mktemp -d)
ld -r -o "$$work/combined.o" --whole-archive $(SRCS) --no-whole-archive
objcopy --wildcard \
  --localize-symbol '*CaDiCaL*' \
  --localize-symbol 'ccadical*' \
  "$$work/combined.o" "$$work/local.o"
rm -f $@
ar rcs $@ "$$work/local.o"
rm -rf "$$work"
""",
)

cc_import(
    name = "cvc5_combined_import",
    static_library = "libcvc5_combined.a",
)

# The downstream-facing cvc5: combined+localized lib + headers + dynamic GMP.
# Everything that links cvc5 (smt-switch backend objects, Pono, pass/lec) uses
# this, so only the localized CaDiCaL reaches the final link.
cc_library(
    name = "cvc5",
    hdrs = glob(["include/**/*.h"]),
    includes = ["include"],
    linkopts = [
        "-lgmpxx",
        "-lgmp",
        "-lpthread",
        "-ldl",
        "-lm",
    ],
    visibility = ["//visibility:public"],
    deps = [":cvc5_combined_import"],
)
