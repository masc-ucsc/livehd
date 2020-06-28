#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

cc_library(
    name = "pass_cprop",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
        "//pass/common:pass",
        "//lemu:lemu",
        "//lgcpp/common:lgcpp",
    ]
)

