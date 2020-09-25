#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "pass_compiler",
    srcs = glob(["*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
        # "//pass/bitwidth:pass_bitwidth",
        # "//pass/cprop:pass_cprop",
        "//pass/lnast_tolg:pass_lnast_tolg",
        "//pass/common:pass",
        "//inou/graphviz:inou_graphviz",
    ],
)


