cc_library(
    name = "inou_lef",
    srcs = ["inou_lef.cpp"],
    hdrs = ["inou_lef.hpp"],
    visibility = ["//visibility:public"],
    deps = [
        "//misc/lef/lef:lef",
        "//core:core"
    ],
)

cc_binary(
    name = "lglef",
    srcs = ["lglef.cpp"],
    linkopts = ['-lpthread'],
    deps = [
        ":inou_lef",
        "//core:core", "//inou/json:inou_json",
        "@boost//:filesystem", "@boost//:program_options",
    ],
)
