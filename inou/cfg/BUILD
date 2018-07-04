#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "inou_cfg",
    srcs = glob(["*.cpp"], exclude=["lgcfg.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
        "//meta:meta",
        "//pyth:py_base",
    ],
)

exports_files(["tests/pt1.cfg", "tests/pt1.cfg"])