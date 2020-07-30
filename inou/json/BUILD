#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "inou_json",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
        "//elab:elab",
        "//pass/common:pass",
    ],
)


sh_test(
    name = "rand_json.sh",
    srcs = ["tests/rand_json.sh"],
    data = [
        "//main:lgshell",
        ],
)
