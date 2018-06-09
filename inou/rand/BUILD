cc_library(
    name = "inou_rand",
    srcs = ["inou_rand.cpp"],
    hdrs = ["inou_rand.hpp"],
    visibility = ["//visibility:public"],
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
