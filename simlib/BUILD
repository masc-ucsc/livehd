#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "simlib",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
        "@iassert//:iassert",
        "@com_google_absl//absl/container:flat_hash_map",
        ],
    )

cc_test(
    name = "simlib_number_test",
    srcs = ["tests/simlib_number_test.cpp"],
    deps = [
        ":simlib",
        "@gtest//:gtest_main",
        "@fmt//:fmt",
        ],
    )

