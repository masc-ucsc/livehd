cc_library(
    name = "py_base",
    srcs = ["py_options.cpp"],
    hdrs = ["py_options.hpp"],
    visibility = ["//visibility:public"],
    linkopts = ['-lpython3.6m', '-lpthread'],
    includes = ["."],
    deps = [
        "//core:core",
        "//meta:meta",
        "@python3//:headers",
        "@pybind11//:headers",
    ],
)

cc_library(
    name = "py_lgraph",
    srcs = ["py_lgraph.cpp"],
    hdrs = ["py_lgraph.hpp"],
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
        "//core:core",
        "//inou/rand:inou_rand",
        "//inou/abc:inou_abc",
        "//inou/cfg:inou_cfg",
        "//pass/dfg:pass_dfg",
    ],
)

load("//tools:linkso.bzl", "linkso")

linkso(
    name = "lgraph.cpython-36m-x86_64-linux-gnu",
		srcs   = [":py_lgraph",
		        ":py_base",
						"//pass/dfg:pass_dfg",
						"@abc//:abc",
						"//inou/lef:inou_lef", "//misc/lef/lef:lef",
						"//inou/abc:inou_abc",
						"//inou/cfg:inou_cfg",
						"//inou/rand:inou_rand",
						"//core:core",
						"//meta:meta",
            ],
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

