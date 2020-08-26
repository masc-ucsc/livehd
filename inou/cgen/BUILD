#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

cc_library(
    name = "inou_cgen",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
        "//pass/common:pass",
    ],
    data = [
        "//inou/pyrope:pyrope_tests"
    ],
)

