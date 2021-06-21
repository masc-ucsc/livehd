# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

VERILOG_TESTS = glob([
    "*.v",
])

filegroup(
    name = "verilog_tests",
    srcs = VERILOG_TESTS,
    visibility = ["//visibility:public"],
)

sh_test(
    name = "lgshell_test.sh",
    srcs = ["lgshell_test.sh"],
    data = [
        ":verilog_tests",
        "//main:lgshell",
    ],
    tags = ["fixme"],
    deps = [
        "//inou/yosys:scripts",
    ],
)

[sh_test(
    name = "verilog.sh-%s" % t,
    srcs = ["verilog.sh"],
    args = ["$(location %s)" % t],
    data = [
        "%s" % t,
        "//main:lgshell",
    ],
    tags = [
        "fixme",
        "verilog.sh",
    ],
    deps = [
        "//inou/yosys:scripts",
    ],
) for t in VERILOG_TESTS]
