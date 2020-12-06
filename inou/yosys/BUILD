#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

cc_library(
    name = "lgyosys",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp", "yosys_driver.cpp"]),
    hdrs = glob(["*.hpp"]),
    #alwayslink=1,
    alwayslink=True,
    defines = [
        "_YOSYS_",
        # "YOSYS_ENABLE_PLUGINS",
        # "YOSYS_ENABLE_READLINE",
        "YOSYS_ENABLE_GLOB",
        #"YOSYS_ENABLE_TCL",
        "YOSYS_ENABLE_ABC",
        "YOSYS_ENABLE_COVER",
        'ABCEXTERNAL=\\"abc\\"',
    ],
    features = ["-use_header_modules"],
    visibility = ["//visibility:public"],
    deps = [
        "//pass/common:pass",
        "@mustache//:headers",
        "@yosys//:kernel",
        "@yosys//:version",
    ],
)

sh_library(
    name = "scripts",
    srcs = [
        "lgcheck",
        ":yosys2",
    ],
    visibility = ["//visibility:public"],
)

filegroup(
    name = "verilog_tests",
    srcs = glob([
        "tests/*.v",
    ]),
    visibility = ["//visibility:public"],
)

filegroup(
    name = "inou_yosys_scripts",
    srcs = glob([
        "inou_yosys_*.ys",
    ]),
    visibility = ["//visibility:public"],
)

cc_binary(
    name = "yosys2",
    srcs = [
        "yosys_driver.cpp",
    ],
    features = ["-use_header_modules"],
    visibility = ["//visibility:public"],
    linkstatic=True,
    deps = [
        ":lgyosys",
        "//core:core",
        #"@dk_thrysoee_libedit//:pretend_to_be_gnu_readline_system",
    ],
)
