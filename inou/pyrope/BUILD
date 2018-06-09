
cc_library(
    name = "inou_pyrope",
    srcs = ["inou_pyrope.cpp"],
    hdrs = ["inou_pyrope.hpp"],
    visibility = ["//visibility:public"],
    deps = [
        "//core:core"
    ],
)

cc_binary(
    name = "lgpyrope",
    srcs = ["lgpyrope.cpp"],
    linkopts = ['-lpthread'],
    deps = [
        ":inou_pyrope",
        "//core:core",
        "@boost//:filesystem", "@boost//:program_options",
    ],
)
