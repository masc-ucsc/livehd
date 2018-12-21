#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "core",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = ["@bm//:headers",
            "@rapidjson//:headers",
            "//eprp:eprp",
            "//lmms:headers",
            "//bench:headers",
            ],
)

cc_test(
    name = "edge_test",
    srcs = ["tests/edge_test.cpp"],
    deps = [
        ":core",
        "//meta:meta",
    ],
)

cc_test(
    name = "iter_test",
    srcs = ["tests/iter_test.cpp"],
    deps = [
        ":core",
        "//meta:meta",
    ],
)

