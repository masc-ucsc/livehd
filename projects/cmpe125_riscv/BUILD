load("//tools:genccs.bzl", "genccs")
load("//tools:genccs.bzl", "verilator_run")

genccs(
    name = "verilator_obj_dir2",
    generator = ":genccs",
)

verilator_run(
    name = "veri_obj",
    #verilator = "/usr/bin/verilator",
    srcs = glob(["rtl/*v"])
         + glob(["cpp/*.cpp"]),
    args = ["--trace",
            "--top-module","pipe"
            ],
    visibility = ["//visibility:public"],
)

cc_binary(
    name = "Vpipe",
    srcs = [ "cpp/dut_bootstrap.cpp" ],
    deps = [":Vpipe_lib",
            ],
)

cc_library(
    name = "Vpipe_lib",
    srcs = [ "veri_obj" ],
    deps = ["@verilator//:verilated",
            "//projects/cmpe125_riscv/cpp/riscv_verify:riscv_verify",
            ],
)

cc_library(
    name = "Vpipe2",
    srcs = [ "verilator_obj_dir2" ],
)

cc_binary(
    name = "genccs",
    srcs = [ "genccs.cpp" ],
)

