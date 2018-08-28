#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

cc_library(
    name = "inou_pyrope",
    srcs = ["inou_pyrope.cpp"],
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
        "//meta:meta",
        "//pyth:py_base"
    ],
)

cc_binary(
    name = "lgpyrope",
    srcs = ["lgpyrope.cpp"],
    linkopts = ['-lpthread'],
    deps = [
        ":inou_pyrope",
        "@boost//:filesystem", "@boost//:program_options",
    ],
)
