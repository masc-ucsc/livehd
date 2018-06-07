
cc_library(
    name = "inou_cfg",
    srcs = glob(["*.cpp"], exclude=["lgcfg.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    deps = [
        "//core:core",
    ],
)

cc_binary(
    name = "lgcfg",
    srcs = ["lgcfg.cpp"],
    linkopts = ['-lpthread'],
    deps = [
        ":inou_cfg",
    ],
)
