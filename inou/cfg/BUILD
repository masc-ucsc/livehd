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

exports_files(["tests/pt1.cfg",
               "tests/pt2.cfg",
               "tests/pt3.cfg",
               "tests/pt4.cfg",
               "tests/pt5.cfg",
               "tests/pt6.cfg",
               "tests/pt7.cfg",
               "tests/pt8.cfg",
               "tests/pt9.cfg",
               "tests/pt10.cfg",
               "tests/pt11.cfg",
               "tests/pt12.cfg",
               "tests/pt13.cfg",
               "tests/pt14.cfg"])
