load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "verilated",
    srcs = glob(
        ["include/*.cpp"],
        exclude = ["include/verilated_vcd_sc.cpp"],
    ),
    hdrs = glob(["include/*.h"]) + glob([
        "include/vltstd/*.h",
    ]) + glob([
        "include/gtkwave/*.h",
    ]) + glob([
        "include/gtkwave/*.c",
    ]) + glob([
        "include/gtkwave/*.cpp",
    ]),
    copts = [
        "-w",
        "-O2",
    ],
    defines = ["VL_THREADED=1"],
    includes = [
        "include/",
        "include/vltstd",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "verilated_vcd_c",
    srcs = [
        "include/verilated.h",
        "include/verilated_config.h",
        "include/verilated_heavy.h",
        "include/verilated_imp.h",
        "include/verilated_syms.h",
        "include/verilated_vcd_c.cpp",
        "include/verilatedos.h",
    ],
    hdrs = [
        "include/verilated_vcd_c.h",
    ],
    copts = [
        "-w",
        "-O2",
    ],
    includes = ["include/"],
    visibility = ["//visibility:public"],
)
