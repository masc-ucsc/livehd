# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "pass_opentimer",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
        "//meta:meta",
        "//core:core",
        "@opentimer//:opentimer",
    ]
)
