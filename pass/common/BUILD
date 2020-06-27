#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "pass",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
        "//core:core",
    ]
)
