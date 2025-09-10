# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")

#cc_import(
#   name = "yosysincludes",
#   hdrs = glob(["kernel/*.h"]),
#   visibility = ["//visibility:public"],
#)

cc_library(
    name = "abc",
    srcs = glob(
               ["src/base/**/*.c"],
               exclude = [
                   "src/base/abci/abcDarUnfold2.c",
                   "src/base/abci/abciUnfold2.c",
                   "src/base/abci/abcPlace.c",
                   "src/base/abci/fahout_cut.c",
                   "src/base/abci/abcEspresso.c",
                   "src/base/pla/plaFxch.c",
                   "src/base/main/main.c",
                   "src/base/main/mainMC.c",
               ],
           ) +
           glob(
               ["src/aig/**/*.c"],
               exclude = [
                   "src/aig/gia/giaCTas2.c",
                   "src/aig/gia/giaProp.c",
                   "src/aig/gia/giaMffc.c",
                   "src/aig/gia/giaSat.c",
                   "src/aig/aig/aigTest.c",
                   "src/aig/aig/aigRepar.c",
                   "src/aig/ivy/ivyMulti8.c",
                   "src/aig/ivy/ivyRwrAlg.c",
                   "src/aig/saig/saigUnfold2.c",
                   "src/aig/saig/saigRefSat.c",
                   "src/aig/hop/cudd2.c",
               ],
           ) +
           glob(
               ["src/bool/**/*.c"],
               exclude = [
                   "src/bool/kit/kitPerm.c",
               ],
           ) +
           glob(
               ["src/bdd/**/*.c"],
               exclude = [
                   "src/bdd/bbr/bbr_.c",
                   "src/bdd/cudd/test*.c",
               ],
           ) +
           glob(
               ["src/map/**/*.c"],
               exclude = [
                   "src/map/mio/mioForm.c",
                   "src/map/if/ifCheck.c",
                   "src/map/cov/covTest.c",
                   #   "src/map/fpga/fpgaTruth.c",
               ],
           ) +
           glob(
               ["src/misc/**/*.c"],
               exclude = [
                   "src/misc/parse/parseCore.c",
                   "src/misc/espresso/*.c",
                   "src/misc/avl/*.c",
                   "src/misc/zlib/*.c",
               ],
           ) +
           glob(
               ["src/opt/**/*.c"],
               exclude = [
                   "src/opt/res/resSim_old.c",
                   "src/opt/fsim/*.c",
                   "src/opt/nwk/nwkFlow_depth.c",
                   "src/opt/dau/dauDsd2.c",
                   "src/opt/mfs/*_.c",
                   "src/opt/mfs/mfsGia.c",
                   "src/opt/cut/abcCut.c",
               ],
           ) +
           glob(
               ["src/proof/**/*.c"],
               exclude = [
                   "src/proof/cec/cec.c",
                   "src/proof/abs/absRefJ.c",
                   "src/proof/int2/*.c",
               ],
           ) +
           glob(
               [
                   "src/sat/**/*.c",
                   "src/sat/**/*.cpp",
               ],
               exclude = [
                   "src/sat/msat/msatOrderJ.c",
                   "src/sat/bsat/satChecker.c",
                   "src/sat/bsat2/*.cpp",
               ],
           ),
    hdrs = glob(["src/**/**/*.h"]) + glob([
        "src/**/**/*Unfold2.c",
    ]),
    copts = [
        "-w",
        "-O2",
    ],  # Always fast, no warnings to avoid spurious messages
    defines = [
        "ABC_NO_DYNAMIC_LINKING=1",
        "LIN64",
        "SIZEOF_VOID_P=8",
        "SIZEOF_LONG=8",
        "SIZEOF_INT=8",
        "ABC_USE_CUDD=1",
        "ABC_USE_PTHREADS",
    ],
    includes = ["src"],
    visibility = ["//visibility:public"],
)

#cc_library(
#   name = "yosyslib",
#   srcs = glob(["techlibs/common/*.cc","frontends/**/*.cc","passes/**/*.cc", "backends/**/*.cc"],
#               exclude=["passes/techmap/libparse.cc",  # included
#                        "frontends/verific/verificsva.cc", "frontends/verific/verific.cc", # no verific frontend
#                       ]
#              ),
#   hdrs = glob(["techlibs/common/sim*.inc",
#                "frontends/**/*.h","passes/**/*.h",
#                "passes/techmap/libparse.cc",
#                "passes/techmap/techmap.inc",
#                "backends/**/*.h",
#                ]),
#   defines = ["_YOSYS_","YOSYS_ENABLE_READLINE","YOSYS_ENABLE_PLUGINS","YOSYS_ENABLE_ABC","YOSYS_ENABLE_TCL"],
#   visibility = ["//visibility:public"],
#    deps = [
#        ":yosyskernel"
#        ":bigint",
#        ":sha1",
#        ":ezsat",
#        ":subcircuit",
#        "@ffi//:ffi",
#    ],
#)
#
#
#
#cc_library(
#   name = "bigint",
#   srcs = glob(["libs/bigint/*.cc"],exclude=["libs/bigint/testsuite.cc","libs/bigint/sample.cc"]),
#   hdrs = glob(["libs/bigint/*.hh"]),
#   visibility = ["//visibility:public"],
#)
#
#cc_library(
#   name = "sha1",
#   srcs = glob(["libs/sha1/*.cpp"]),
#   hdrs = glob(["libs/sha1/*.h"]),
#   visibility = ["//visibility:public"],
#)
#
#cc_library(
#   name = "ezsat",
#   srcs = glob(["libs/ezsat/ez*.cc","libs/minisat/*.cc"]),
#   hdrs = glob(["libs/ezsat/ez*.h","libs/minisat/*.h"]),
#   visibility = ["//visibility:public"],
#)
#
#cc_library(
#   name = "subcircuit",
#   srcs = glob(["libs/subcircuit/s*.cc"]),
#   hdrs = glob(["libs/subcircuit/*.h"]),
#   visibility = ["//visibility:public"],
#)
#
#cc_binary(
#    name = "yosys",
#    srcs = ["kernel/driver.cc"],
#    linkopts = ['-lreadline','-ltcl','-lpthread'],
#    deps = [
#        ":yosyslib",
#    ],
#)
