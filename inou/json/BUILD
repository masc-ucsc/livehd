#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "inou_json",
    srcs = ["inou_json.cpp"],
    hdrs = ["inou_json.hpp"],
    visibility = ["//visibility:public"],
    deps = [
        "//meta:meta"
    ],
)

cc_binary(
    name = "lgjson",
    srcs = ["lgjson.cpp"],
    linkopts = ['-lpthread'],
    #linkopts = ['-static', '-lpthread'],
    deps = [
        ":inou_json",
    ],
)
