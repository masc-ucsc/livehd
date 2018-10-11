#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "verilog",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = ["//eprp:eprp",
    ]
)

cc_binary(
    name = "verilog_test",
    srcs = ["verilog_test.cpp"],
    # args = ["$(location tests/synth1.eprp)"],
    deps = [
        ":verilog",
    ],
)

