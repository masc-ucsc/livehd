cc_library(
    name = "py_lgraph",
    srcs = ["py_lgraph.cpp"],
    visibility = ["//visibility:public"],
    deps = [
        "//core:core",
        "//inou/rand:inou_rand",
        "//inou/abc:inou_abc",
    ],
)

load("//tools:linkso.bzl", "linkso")

linkso(
    name = "lgraph.cpython-36m-x86_64-linux-gnu",
    srcs   = [":py_lgraph","//inou/rand:inou_rand","//inou/abc:inou_abc","//core:core"],
    static = ["@boost//:system" , "@boost//:filesystem", "@boost//:program_options"],
)

load("@subpar//:subpar.bzl", "par_binary")

par_binary(
    name = 'lgraph',
    srcs = ['lgraph.py'],
    default_python_version = "PY3",
    data = ["lgraph.cpython-36m-x86_64-linux-gnu.so"],
    zip_safe = False,
)

py_test(
    name = 'test_inou_rand',
    srcs = ['test_inou_rand.py'],
    deps = [":lgraph"],
)

py_test(
    name = 'test_core1',
    srcs = ['test_core1.py'],
    deps = [":lgraph"],
)

