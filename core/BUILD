#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "core",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = ["@bm//:headers",
            "@rapidjson//:headers",
            "@iassert//:iassert",
            "//eprp:eprp",
            "//lmms:headers",
            "//bench:headers",
            "@yas//:headers",
            ],
)

cc_test(
    name = "attribute_test",
    srcs = ["tests/attribute_test.cpp"],
    deps = [
        "@gtest//:gtest_main",
        ":core",
    ],
)

