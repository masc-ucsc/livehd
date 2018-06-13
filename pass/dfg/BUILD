cc_library(
    name = "pass_dfg",
    srcs = glob(["*.cpp"], exclude=["lgdfg.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
        "//core:core",
        "//inou/cfg:inou_cfg",
    ],
)

