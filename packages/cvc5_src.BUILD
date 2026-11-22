# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# Offline source build: dependency archives are pinned by cvc5_repo.bzl.
load("@LIVEHD_INPUTS@", "CVC5_MACHO_TOOLS", "cvc5_header", "cvc5_macho_archive", "cvc5_platform_variables", "cvc5_python")
load("@rules_cc//cc:defs.bzl", "cc_import", "cc_library")
load("@rules_foreign_cc//foreign_cc:defs.bzl", "cmake")
cvc5_header(name = "gmp_header", library = "@gmp//:gmp", basename = "gmp.h")
cvc5_header(name = "gmpxx_header", library = "@gmp//:gmpxx", basename = "gmpxx.h")
cvc5_python(name = "python_runtime")
cvc5_platform_variables(name = "platform_variables")
filegroup(name = "python_executable", srcs = [":python_runtime"], output_group = "interpreter")
filegroup(name = "source", srcs = glob(["**"], exclude = ["BUILD.bazel", "livehd_sat_bridge.cpp", "satheaders/**"]))
filegroup(name = "gmp_archive", srcs = ["@gmp//:gmp"], output_group = "archive")
filegroup(name = "gmpxx_archive", srcs = ["@gmp//:gmpxx"], output_group = "archive")
cmake(
    name = "build",
    lib_source = ":source",
    generate_args = ["-GNinja"],
    # Honor the standard CMake resource control for bounded cold builds. Keep
    # the existing eight-job fallback when the caller does not specify one.
    # Double-dollar escaping survives Bazel Make expansion into the shell.
    build_args = ["--parallel", '"$${CMAKE_BUILD_PARALLEL_LEVEL:-8}"'],
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
    # Clang also honors this in nested configure/make subprocesses whose
    # build systems do not forward the parent's CMAKE_C/CXX_FLAGS.
    env = select({
        "@platforms//os:macos": {"CCACHE_DISABLE": "1", "MACOSX_DEPLOYMENT_TARGET": "$(CVC5_MACOS_MINIMUM_OS)"},
        "//conditions:default": {"CCACHE_DISABLE": "1"},
    }),
    toolchains = [":platform_variables"],
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
cc_library(name = "sat_bridge", srcs = ["livehd_sat_bridge.cpp"], hdrs = glob(["satheaders/cadical/src/*.hpp", "satheaders/cms/src/*.h"]))
filegroup(name = "sat_bridge_archive", srcs = [":sat_bridge"], output_group = "archive")
genrule(name = "combined", toolchains = [":platform_variables"], srcs = [':sat_bridge_archive', ':libcvc5.a', ':libcadical.a', ':libpicpoly.a', ':libpicpolyxx.a', ':libkissat.a', ':libcryptominisat5.a'], outs = ["libcvc5_combined.a"],
    tools = CVC5_MACHO_TOOLS,
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
  # ABC also bundles Kissat/Kitten, including helpers without a kissat_ prefix.
  nm --defined-only --extern-only $(location :libcadical.a) $(location :libkissat.a) \
    | awk '$$2 ~ /^[TDBRC]$$/ {print $$3}' > "$$work/cad.syms"
  # Include solver templates emitted by libcvc5 and local COMDAT signatures
  # (e.g. destructor D5 symbols), so ELF cannot fold them with ABC's groups.
  nm --defined-only "$$work/combined.o" \
    | awk '{print $$3}' \
    | grep -E 'CaDiCaL|4Reap|^ipasir_|^ccadical|^kissat_|^kitten_|^new_learned_klause$$|CMSat|^cmsat_' >> "$$work/cad.syms"
  sort -u "$$work/cad.syms" -o "$$work/cad.syms"
  awk '{print $$1, "lhdcvc5priv_" $$1}' "$$work/cad.syms" > "$$work/cad.redef"
  objcopy --redefine-syms="$$work/cad.redef" \
    "$$work/combined.o" "$$work/local.o"
  rm -f $@
  ar rcs $@ "$$work/local.o"
""",
    }),
)
cc_import(name = "combined_import", static_library = "libcvc5_combined.a")
cc_library(name = "cvc5_headers", defines = ["LIVEHD_CVC5_SOURCE_BUILD"], hdrs = [":installed_headers"], includes = ["build/include"], visibility = ["//visibility:public"])
cc_library(name = "cvc5", deps = [":cvc5_headers", ":combined_import", "@gmp//:gmp", "@gmp//:gmpxx"], linkopts = ["-lpthread", "-ldl", "-lm"], visibility = ["//visibility:public"])

# The public model-producing C API uses the same private solvers as cvc5.
cc_library(name = "sat_solver", deps = [":cvc5"], visibility = ["//visibility:public"])

# A prebuilt cvc5 can coexist with placement SAT without pulling a second
# libcvc5 into lhd. Reuse the SAME pinned source-built SAT archives, but isolate
# this bridge/dependency object under its own prefix (including COMDAT keys).
genrule(
    name = "sat_standalone_combined",
    srcs = [":sat_bridge_archive", ":libcadical.a", ":libcryptominisat5.a"],
    outs = ["liblivehd_sat.a"],
    toolchains = [":platform_variables"],
    tools = CVC5_MACHO_TOOLS,
    cmd = """
set -e
work=$$(mktemp -d "$(@D)/sat-XXXXXX")
trap 'rm -rf "$$work"' EXIT
""" + select({
        "@platforms//os:macos": cvc5_macho_archive("lhdsatpriv_"),
        "//conditions:default": """
  ld -r -o "$$work/combined.o" --whole-archive $(SRCS) --no-whole-archive
  nm --defined-only --extern-only $(location :libcadical.a) $(location :libcryptominisat5.a) \
    | awk '$$2 ~ /^[TDBRC]$$/ {print $$3}' > "$$work/private.syms"
  nm --defined-only "$$work/combined.o" | awk '{print $$3}' \
    | grep -E 'CaDiCaL|4Reap|^ipasir_|^ccadical|CMSat|^cmsat_' >> "$$work/private.syms"
  sort -u "$$work/private.syms" | awk '{print $$1, "lhdsatpriv_" $$1}' > "$$work/private.redef"
  objcopy --redefine-syms="$$work/private.redef" "$$work/combined.o" "$$work/private.o"
  ar rcs $@ "$$work/private.o"
""",
    }),
)
cc_import(name = "sat_standalone_import", static_library = "liblivehd_sat.a")
cc_library(name = "sat_standalone", deps = [":sat_standalone_import"], linkopts = ["-lpthread", "-lm"], visibility = ["//visibility:public"])
