# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# Offline source build: dependency archives are pinned by cvc5_repo.bzl.
load("@LIVEHD_INPUTS@", "cvc5_header", "cvc5_python")
load("@rules_cc//cc:defs.bzl", "cc_import", "cc_library")
load("@rules_foreign_cc//foreign_cc:defs.bzl", "cmake")
cvc5_header(name = "gmp_header", library = "@gmp//:gmp", basename = "gmp.h")
cvc5_header(name = "gmpxx_header", library = "@gmp//:gmpxx", basename = "gmpxx.h")
cvc5_python(name = "python_runtime")
filegroup(name = "python_executable", srcs = [":python_runtime"], output_group = "interpreter")
filegroup(name = "source", srcs = glob(["**"], exclude = ["BUILD.bazel"]))
filegroup(name = "gmp_archive", srcs = ["@gmp//:gmp"], output_group = "archive")
filegroup(name = "gmpxx_archive", srcs = ["@gmp//:gmpxx"], output_group = "archive")
cmake(
    name = "build",
    lib_source = ":source",
    generate_args = ["-GNinja"],
    build_args = ["-j8"],
    cache_entries = {
        "CMAKE_BUILD_TYPE": "Production",
        "CMAKE_POSITION_INDEPENDENT_CODE": "ON",
        "CMAKE_CXX_STANDARD_LIBRARIES": "-l@CXX_LIB@ -lm",
        "BUILD_SHARED_LIBS": "OFF",
        "STATIC_BINARY": "OFF",
        "ENABLE_GPL": "OFF",
        "ENABLE_SAFE_MODE": "OFF",
        "ENABLE_STABLE_MODE": "OFF",
        "ENABLE_AUTO_DOWNLOAD": "OFF",
        "USE_PYTHON_VENV": "OFF",
        "USE_MPFR": "OFF",
        "USE_POLY": "ON",
        "USE_KISSAT": "ON",
        "USE_CRYPTOMINISAT": "ON",
        # Native object symbols must survive the ld -r/objcopy isolation step.
        "ENABLE_IPO": "OFF",
        "ENABLE_UNIT_TESTING": "OFF",
        "BUILD_BINDINGS_PYTHON": "OFF",
        "BUILD_BINDINGS_JAVA": "OFF",
        "BUILD_DOCS": "OFF",
        "LIVEHD_LIBRARY_ONLY": "ON",
        "LIVEHD_OFFLINE_DEPS": "ON",
        "LIVEHD_GMP_HEADER": "$$EXT_BUILD_ROOT$$/$(execpath :gmp_header)",
        "GMP_LIBRARIES": "$$EXT_BUILD_ROOT$$/$(execpath :gmp_archive)",
        "LIVEHD_GMPXX_HEADER": "$$EXT_BUILD_ROOT$$/$(execpath :gmpxx_header)",
        "GMPXX_LIBRARIES": "$$EXT_BUILD_ROOT$$/$(execpath :gmpxx_archive)",
        "Python_EXECUTABLE": "$$EXT_BUILD_ROOT$$/$(execpath :python_executable)",
        "CMAKE_INSTALL_LIBDIR": "lib",
    },
    env = {"CCACHE_DISABLE": "1"},
    data = [":gmp_archive", ":gmpxx_archive", ":gmp_header", ":gmpxx_header", ":python_runtime", ":python_executable"],
    out_static_libs = ['libcvc5.a', 'libcadical.a', 'libpicpoly.a', 'libpicpolyxx.a', 'libkissat.a', 'libcryptominisat5.a'],
)
filegroup(name = "libcvc5.a", srcs = [":build"], output_group = "libcvc5.a")
filegroup(name = "libcadical.a", srcs = [":build"], output_group = "libcadical.a")
filegroup(name = "libpicpoly.a", srcs = [":build"], output_group = "libpicpoly.a")
filegroup(name = "libpicpolyxx.a", srcs = [":build"], output_group = "libpicpolyxx.a")
filegroup(name = "libkissat.a", srcs = [":build"], output_group = "libkissat.a")
filegroup(name = "libcryptominisat5.a", srcs = [":build"], output_group = "libcryptominisat5.a")
filegroup(name = "installed_headers", srcs = [":build"], output_group = "include")
genrule(name = "combined", srcs = [':libcvc5.a', ':libcadical.a', ':libpicpoly.a', ':libpicpolyxx.a', ':libkissat.a', ':libcryptominisat5.a'], outs = ["libcvc5_combined.a"],
    cmd = """
set -e
work=$$(mktemp -d "$(@D)/cvc5-XXXXXX")
trap 'rm -rf "$$work"' EXIT
if [ "$$(uname)" = "Darwin" ]; then
  clang -nostdlib -Wl,-r -Wl,-all_load \
    -Wl,-unexported_symbol,'*CaDiCaL*' \
    -Wl,-unexported_symbol,'*ccadical*' \
    -Wl,-unexported_symbol,'*Reap*' \
    -Wl,-unexported_symbol,'ipasir_*' \
    -Wl,-unexported_symbol,'kissat_*' \
    -Wl,-unexported_symbol,'kitten_*' \
    -Wl,-unexported_symbol,'new_learned_klause' \
    -Wl,-unexported_symbol,'*CMSat*' \
    -Wl,-unexported_symbol,'cmsat_*' \
    -o "$$work/combined.o" $(SRCS)
  rm -f $@
  ar rcs $@ "$$work/combined.o"
else
  ld -r -o "$$work/combined.o" --whole-archive $(SRCS) --no-whole-archive
  # Collect the CaDiCaL symbols to privatize. Start with every strong global
  # (T/D/B/R) that libcadical.a defines, by name. ld -r has already bound cvc5's
  # internal references within combined.o, so privatizing these keeps cvc5's own
  # CaDiCaL intact while taking it out of the global namespace it shares with abc.
  nm --defined-only --extern-only $(location :libcadical.a) \
    | awk '$$2 ~ /^[TDBR]$$/ {print $$3}' | sort -u > "$$work/cad.syms"
  # ABC also bundles Kissat/Kitten, including helpers without a kissat_ prefix.
  nm --defined-only --extern-only $(location :libkissat.a) \
    | awk '$$2 ~ /^[TDBR]$$/ {print $$3}' >> "$$work/cad.syms"
  # The libcadical.a sweep above is NOT enough: cvc5's own code (the
  # `cvc5::internal::prop::cadical` CadicalPropagator/tracers) #includes
  # CaDiCaL headers, so it instantiates CaDiCaL templates (heap<>, rsort<>),
  # emits CaDiCaL vtables/typeinfo (StatTracer, ClauseCopier/Writer/Counter,
  # ...) and the Reap class + ipasir_/ccadical C API DIRECTLY INTO libcvc5.a,
  # NOT libcadical.a. These are WEAK (COMDAT) symbols, so they do not trip a
  # duplicate-symbol link error -- instead they silently COMDAT-fold with
  # abc's own vendored CaDiCaL (its `class Reap` is also global-namespace and
  # it uses `namespace CaDiCaL`). The linker keeps ONE arbitrary copy; cvc5's
  # solver then dispatches through abc's incompatible CaDiCaL vtables/heaps and
  # corrupts memory -> SIGSEGV deep in inprocessing (vivify/subsume/elim) on the
  # larger SAT instances (e.g. BMC of memory equivalence). macOS's
  # `-unexported_symbol '*CaDiCaL*'` wildcard already hides this whole surface,
  # so it was Mac-green / Linux-crash. Localize the same surface here: every
  # symbol whose mangled name carries the CaDiCaL `7CaDiCaL` namespace tag
  # (covers `_ZN7CaDiCaL...`, vtables `_ZTVN7CaDiCaL`, typeinfo `_ZTIN/_ZTSN`,
  # thunks, and cvc5's own cadical glue -- all cvc5-private after `ld -r`),
  # plus the global-namespace Reap class and the ipasir_/ccadical C APIs.
  # Include LOCAL symbols too: destructor COMDAT signatures such as
  # _ZN7CaDiCaL7WrapperD5Ev are local NOTYPE symbols (nm type 'n'). Renaming
  # only the exported D0/D1/D2 functions leaves that group signature shared
  # with abc, so the linker can discard cvc5's renamed destructor sections.
  nm --defined-only "$$work/combined.o" \
    | awk '{print $$3}' \
    | grep -E 'CaDiCaL|4Reap|^ipasir_|^ccadical|^kissat_|CMSat|^cmsat_' >> "$$work/cad.syms"
  sort -u "$$work/cad.syms" -o "$$work/cad.syms"
  # RENAME, don't just localize: CaDiCaL's WEAK template/vtable/typeinfo symbols
  # live in ELF COMDAT groups keyed by the symbol NAME. objcopy --localize-symbols
  # changes a symbol's binding but NOT its name, so cvc5's group keeps the same
  # signature as abc's identically-named group; lld then discards one of the two
  # groups and cvc5's (now local) relocations dangle into the discarded section
  # ("relocation refers to a symbol in a discarded section"). Renaming each
  # CaDiCaL symbol to a cvc5-private name also renames its COMDAT group
  # signature, so cvc5's groups never collide with abc's -- both copies survive
  # and each side binds to its own CaDiCaL. objcopy rewrites the matching
  # relocations (which reference symbols by index), so combined.o stays
  # self-consistent. (The macOS branch above instead -unexported_symbol's them:
  # Mach-O has no COMDAT-group discard, so localizing is sufficient there.)
  awk '{print $$1, "lhdcvc5priv_" $$1}' "$$work/cad.syms" > "$$work/cad.redef"
  objcopy --redefine-syms="$$work/cad.redef" \
    "$$work/combined.o" "$$work/local.o"
  rm -f $@
  ar rcs $@ "$$work/local.o"
fi
""",
)
cc_import(name = "combined_import", static_library = "libcvc5_combined.a")
cc_library(name = "cvc5_headers", defines = ["LIVEHD_CVC5_SOURCE_BUILD"], hdrs = [":installed_headers"], includes = ["build/include"], visibility = ["//visibility:public"])
cc_library(name = "cvc5", deps = [":cvc5_headers", ":combined_import", "@gmp//:gmp", "@gmp//:gmpxx"], linkopts = ["-lpthread", "-ldl", "-lm"], visibility = ["//visibility:public"])
