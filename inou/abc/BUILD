
cc_library(
    name = "inou_abc",
    srcs = glob(["*.cpp"], exclude=["lgaig.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    deps = [
        "//core:core"
    ],
)

cc_binary(
    name = "lgaig",
    srcs = ["lgaig.cpp"],
    linkopts = ['-lpthread'],
    #linkopts = ['-static', '-lpthread'],
    deps = [
        ":inou_abc",
    ],
)
