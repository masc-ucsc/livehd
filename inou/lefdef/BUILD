cc_library(
    name = "inou_lefdef",
    srcs = glob(["*.cpp"], exclude=["lglefdef.cpp", "defrw.cpp", "lefrw.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    deps = [
        "//core:core",
        "//misc/lef/lef:lef",
        "//misc/def/def:def",
    ],
)

cc_binary(
    name = "lglefdef",
    srcs = ["lglefdef.cpp"],
    linkopts = ['-lpthread'],
    deps = [
        ":inou_lefdef",
        "//core:core", "//inou/json:inou_json",
        "@boost//:filesystem", "@boost//:program_options",
    ],
)

cc_binary(
    name = "defrw",
    srcs = ["defrw.cpp"],
    linkopts = ['-lpthread'],
    deps = [
        ":inou_lefdef",
        "//core:core", "//inou/json:inou_json",
    ],
)

cc_binary(
    name = "lefrw",
    srcs = ["lefrw.cpp"],
    linkopts = ['-lpthread'],
    deps = [
        ":inou_lefdef",
        "//core:core", "//inou/json:inou_json",
    ],
)
