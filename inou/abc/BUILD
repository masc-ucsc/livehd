
cc_library(
    name = "inou_abc",
    srcs = glob(["*.cpp"], exclude=["lgaig.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    deps = [
        "//core:core",
        "//inou/lef:inou_lef",
        "@abc//:abc",
    ],
)

cc_binary(
    name = "lgaig",
    srcs = ["lgaig.cpp"],
    linkopts = ['-ldl', '-lreadline','-lpthread'],
    #linkopts = ['-static', '-lpthread'],
    deps = [
        ":inou_abc",
    ],
)
