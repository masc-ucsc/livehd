load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "simple_test",
    srcs = ["src/simple_test.cpp"],
    copts = [
        "-std=c++20",
        "-I external/mockturtle/include",
    ],
    deps = [
        "//module:mockturtle",
    ],
)
