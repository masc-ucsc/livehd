# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# cvc5 prebuilt non-GPL static libraries (cvc5-<os>-<cpu>-static.zip from the
# official cvc5 releases). This optional escape hatch consumes the prebuilt archive; the
# default @cvc5 alias builds from source. This is the pass/lec SMT backend, used directly via cvc5's
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
# relocatable object and rename the solver symbols so each library binds to
# its own CaDiCaL, including weak definitions.

load("@livehd//packages:cvc5_inputs.bzl", "CVC5_MACHO_TOOLS", "cvc5_macho_archive", "cvc5_platform_variables")
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

cvc5_platform_variables(name = "platform_variables")

# Combine the archives before renaming so all cross-archive references to
# CaDiCaL are updated together.
genrule(
    name = "cvc5_combined",
    # No libcvc5parser.a: the C++-API backend never uses the SMT-LIB parser,
    # and it shares member basenames with libcvc5.a (which would collide).
    srcs = [
        "lib/libcvc5.a",
        "lib/libcadical.a",
        "lib/libpicpoly.a",
        "lib/libpicpolyxx.a",
        "lib/libmpfr.a",  # required by the 1.4.2 prebuilt FP implementation
    ],
    outs = ["libcvc5_combined.a"],
    cmd = """
set -e
work=$$(mktemp -d "$(@D)/cvc5-XXXXXX")
trap 'rm -rf "$$work"' EXIT
""" + select({
        "@platforms//os:macos": cvc5_macho_archive("lhdcvc5priv_"),
        "//conditions:default": """
  ld -r -o "$$work/combined.o" --whole-archive $(SRCS) --no-whole-archive
  # Collect the CaDiCaL symbols to privatize. Start with every strong global
  # (T/D/B/R or tentative C) that libcadical.a defines. ld -r has bound cvc5's
  # internal references within combined.o, so privatizing these keeps cvc5's own
  # CaDiCaL intact while taking it out of the global namespace it shares with abc.
  nm --defined-only --extern-only $(location lib/libcadical.a) \
    | awk '$$2 ~ /^[TDBRC]$$/ {print $$3}' > "$$work/cad.syms"
  # Include CaDiCaL templates emitted by libcvc5 and local COMDAT signatures
  # (e.g. destructor D5 symbols), so ELF cannot fold them with ABC's groups.
  nm --defined-only "$$work/combined.o" \
    | awk '{print $$3}' \
    | grep -E 'CaDiCaL|4Reap|^ipasir_|^ccadical' >> "$$work/cad.syms"
  sort -u "$$work/cad.syms" -o "$$work/cad.syms"
  awk '{print $$1, "lhdcvc5priv_" $$1}' "$$work/cad.syms" > "$$work/cad.redef"
  objcopy --redefine-syms="$$work/cad.redef" \
    "$$work/combined.o" "$$work/local.o"
  rm -f $@
  ar rcs $@ "$$work/local.o"
""",
    }),
    # Pull all archive members directly. Extracting them with ar would overwrite
    # duplicate member basenames and silently lose objects.
    toolchains = [":platform_variables"],
    tools = CVC5_MACHO_TOOLS,
)

cc_import(
    name = "cvc5_combined_import",
    static_library = "libcvc5_combined.a",
)

# The downstream-facing cvc5: combined and isolated lib + headers + hermetic GMP.
# Everything that links cvc5 (pass/lec) uses this, so the private CaDiCaL symbols
# stay distinct at the final link.
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
        "@gmp",
        "@gmp//:gmpxx",
    ],
)
