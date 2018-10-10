load("//tools:genccs.bzl", "genccs")

genccs(
    name = "verilator_obj_dir",
    generator = ":genccs",
)

cc_library(
    name = "Vpipe",
    srcs = [ "verilator_obj_dir" ]
)

cc_binary(
    name = "genccs",
    srcs = [ "genccs.cpp" ],
)

