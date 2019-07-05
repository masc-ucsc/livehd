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
            "//mmap_map:headers",
            "//bench:headers",
            "@yas//:headers",
            ],
)

cc_test(
    name = "hierarchy_test",
    srcs = ["tests/hierarchy_test.cpp"],
    deps = [
        "@gtest//:gtest_main",
        ":core",
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

cc_test(
    name = "lgraph_each",
    srcs = ["tests/lgraph_each_test.cpp"],
    deps = [
        "@gtest//:gtest_main",
        ":core",
    ],
)

cc_test(
    name = "node_test",
    srcs = ["tests/node_test.cpp"],
    deps = [
        "@gtest//:gtest_main",
        ":core",
    ],
)

cc_test(
    name = "iter_test",
    srcs = ["tests/iter_test.cpp"],
    deps = [
        ":core",
    ],
)


cc_test(
    name = "edge_test",
    srcs = ["tests/edge_test.cpp"],
    deps = [
        ":core",
    ],
)
