#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

cc_library(
    name = "pass_abc",
    srcs = glob(["*.cpp"],
                exclude = [
                    "dump_aig.cpp",
                    ]),
    hdrs = glob(["*.hpp"],
                exclude = [
                    "dump_aig.hpp",
                    ]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
        "//meta:meta",
        "//inou/lef:inou_lef",
        "@abc//:abc",
    ],
)

