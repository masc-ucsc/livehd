cc_library(
    name = "inou_rand",
    srcs = ["inou_rand.cpp"],
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
        "//core:core"
    ],
)

cc_binary(
    name = "lgrand",
    srcs = ["lgrand.cpp"],
    linkopts = ['-lpthread'],
    deps = [
        ":inou_rand",
        "//core:core", "//inou/json:inou_json",
        "@boost//:filesystem", "@boost//:program_options",
    ],
)

load("//tools:linkso.bzl", "linkso")

linkso(
    name = "lgraph_inou_rand.cpython-36m-x86_64-linux-gnu",
    srcs   = [":inou_rand", "//core:core"],
    static = ["@boost//:system" , "@boost//:filesystem", "@boost//:program_options"],
)
