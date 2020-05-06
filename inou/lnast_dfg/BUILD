#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "inou_lnast_dfg",
    srcs = glob(["*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
        "//pass/bitwidth:pass_bitwidth",
        "//pass/lgraph_to_lnast:pass_lgraph_to_lnast",
        "//inou/cfg:inou_cfg",
        "//core:core",
    ],
)

