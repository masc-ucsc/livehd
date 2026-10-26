load("@rules_python//python:defs.bzl", "py_library", "py_test")

py_library(
    name = "connectivity",
    srcs = ["connectivity.py"],
    visibility = ["//visibility:public"],
)

py_test(
    name = "connectivity_test",
    srcs = ["connectivity_test.py"],
    deps = [":connectivity"],
    legacy_create_init = 0,
    timeout = "short",
)
