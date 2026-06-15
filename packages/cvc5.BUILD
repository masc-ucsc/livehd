# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# cvc5 prebuilt non-GPL static libraries (cvc5-<os>-<cpu>-static.zip from the
# official cvc5 releases). We consume the prebuilt archive rather than building
# cvc5 from source. This is the pass/lec SMT backend, used directly via cvc5's
# native C++ API (QF_BV combinational equivalence).
#
# LICENSING: keep the default (no-gpl) artifact -- never `-static-gpl.zip`. The
# prebuilt's bundled libgmp.a / libgmpxx.a are not imported; cvc5's (otherwise
# undefined) GMP symbols are satisfied by the hermetic @gmp BCR module (built
# from GMP source), wired in via `deps` below -- no dependency on a system /
# homebrew libgmp. (GMP is LGPLv3 and is statically linked from source here.)
# See pass/lec/lec.md "Build & dependency notes".
#
# SYMBOL ISOLATION: cvc5 and Berkeley-abc (linked into the same `lhd` binary via
# the Yosys flow) BOTH vendor CaDiCaL, with incompatible copies -> duplicate
# strong symbols at the final link. We merge cvc5's static deps into one
# relocatable object and localize the CaDiCaL symbols, so cvc5 keeps a private
# CaDiCaL and abc keeps its own. (Mirrors lhd_export.lds, which localizes
# slang/fmt/boost for the same reason.)

load("@rules_cc//cc:defs.bzl", "cc_import", "cc_library")

# Headers-only view of cvc5 (kept public for any header-only consumer). Pulling
# libs here would re-propagate the un-localized CaDiCaL and reintroduce the
# clash, so this exposes headers only; pass/lec links the full :cvc5 below.
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
    # Combine into one relocatable object pulling EVERY archive member directly
    # (no `ar x` extraction: cvc5's archives have duplicate member basenames that
    # extraction would silently overwrite, dropping objects), then localize the
    # CaDiCaL symbols so they go file-local / non-exported.
    #
    # The toolchains differ by host (the prebuilt is host-specific, so we branch
    # on `uname` rather than a config select):
    #   Linux: GNU `ld -r --whole-archive` + objcopy --localize-symbol.
    #   macOS: ld64 has neither flag and there is no objcopy. Drive ld64 via the
    #          clang driver (auto-supplies -arch/-platform_version) with
    #          -all_load (== --whole-archive) and -unexported_symbol (wildcards),
    #          which localizes the matched symbols during the -r link itself.
    cmd = """
set -e
work=$$(mktemp -d)
if [ "$$(uname)" = "Darwin" ]; then
  clang -nostdlib -Wl,-r -Wl,-all_load \
    -Wl,-unexported_symbol,'*CaDiCaL*' \
    -Wl,-unexported_symbol,'*ccadical*' \
    -o "$$work/combined.o" $(SRCS)
  rm -f $@
  ar rcs $@ "$$work/combined.o"
else
  ld -r -o "$$work/combined.o" --whole-archive $(SRCS) --no-whole-archive
  objcopy --wildcard \
    --localize-symbol '*CaDiCaL*' \
    --localize-symbol 'ccadical*' \
    "$$work/combined.o" "$$work/local.o"
  rm -f $@
  ar rcs $@ "$$work/local.o"
fi
rm -rf "$$work"
""",
)

cc_import(
    name = "cvc5_combined_import",
    static_library = "libcvc5_combined.a",
)

# The downstream-facing cvc5: combined+localized lib + headers + hermetic GMP.
# Everything that links cvc5 (pass/lec) uses this, so only the localized CaDiCaL
# reaches the final link.
cc_library(
    name = "cvc5",
    hdrs = glob(["include/**/*.h"]),
    includes = ["include"],
    linkopts = [
        "-lpthread",
        "-ldl",
        "-lm",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":cvc5_combined_import",
        # cvc5 uses GMP's C (libgmp) and C++ (libgmpxx) interfaces; both are
        # provided hermetically by @gmp (no -lgmp/-lgmpxx against system libs).
        "@gmp//:gmp",
        "@gmp//:gmpxx",
    ],
)
