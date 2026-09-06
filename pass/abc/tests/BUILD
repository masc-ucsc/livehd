load("@rules_cc//cc:defs.bzl", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_test(
    name = "division_smoke",
    srcs = ["division_smoke.cpp", "//pass/abc:abc_arith.hpp"],
    copts = COPTS,
    deps = ["@googletest//:gtest_main"],
)
