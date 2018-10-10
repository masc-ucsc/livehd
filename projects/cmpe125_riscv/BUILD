load("//tools:genccs.bzl", "genccs")

genccs(
    name = "verilator_obj_dir2",
    generator = ":genccs",
)

verilator_run(
    name = "veri_obj",
    #verilator = "/usr/bin/verilator",
    srcs = glob(["rtl/*.sv"]),
    defines = ["--trace"],
)

cc_library(
    name = "Vpipe",
    srcs = [ "veri_obj" ],
)

cc_library(
    name = "Vpipe2",
    srcs = [ "verilator_obj_dir2" ],
)

cc_binary(
    name = "genccs",
    srcs = [ "genccs.cpp" ],
)

