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

sh_test(
    name = "verilog.sh",
    size = "medium",  # give some extra time to the slow machines
    srcs = ["tests/verilog.sh"],
    data = [
        ":lgshell",
        "//inou/yosys:verilog_tests",
    ],
    tags = ["fixme"],
    deps = [
        "//inou/yosys:scripts",
    ],
)
