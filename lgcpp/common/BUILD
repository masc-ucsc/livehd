
cc_library(
    name = "lgcpp",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
        "//core:core",
        "//pass/common:pass",
    ]
)

