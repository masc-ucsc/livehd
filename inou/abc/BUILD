#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

cc_library(
    name = "inou_abc",
    srcs = glob(["*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
        "//meta:meta",
        "//inou/lef:inou_lef",
        "@abc//:abc",
    ],
)

