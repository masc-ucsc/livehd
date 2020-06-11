#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "lemu",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
        "@fmt//:fmt",
        "@boost//:multiprecision",
        "@com_google_absl//absl/types:span",
        "@com_google_absl//absl/strings",
        "//lbench:headers",
        "@iassert//:iassert",
        ],
    )

cc_test(
    name = "lconst_test",
    srcs = ["tests/lconst_test.cpp"],
    deps = [
        ":lemu",
        "@gtest//:gtest_main",
        "//simlib:headers",
        "//mmap_lib:headers",
        ],
    )

cc_test(
    name = "lconst_dump",
    srcs = ["tests/lconst_dump.cpp"],
    deps = [
        ":lemu",
        "//simlib:headers",
        "//mmap_lib:headers",
        ],
    )

