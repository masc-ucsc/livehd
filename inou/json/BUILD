#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "inou_json",
    srcs = ["inou_json.cpp"],
    hdrs = ["inou_json.hpp"],
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
        "//core:core"
    ],
)


sh_test(
    name = "rand_json.sh",
    srcs = ["tests/rand_json.sh"],
    data = [
        "//main:lgshell",
        ],
)
