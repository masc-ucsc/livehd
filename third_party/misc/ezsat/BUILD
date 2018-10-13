#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
licenses(["notice"]) # ISC

cc_library(
    name = "ezsat",
    srcs = glob(["*.cpp"],
                exclude=["demo*",
                         "test*",
                         "sat_lima*",
                         "puzzle*",
                         ]
                ),
    includes = ["."],
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    deps = [
        "@cryptominisat//:cryptominisat",
        ],
)

cc_test(
    name = "testbench",
    srcs = ["testbench.cpp"],
    deps = [
        ":ezsat",
    ],
)
