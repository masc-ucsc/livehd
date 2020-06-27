#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

cc_library(
    name = "inou_cfg",
    srcs = glob(["*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
        "//pass/common:pass",
    ],
)

filegroup(
    name = "cfg_pts",
    srcs = glob(["tests/*.prp", "tests/*.cfg", "tests/verilog_gld/*.v", "tests/dot_gld/*.gld"]),
    visibility = ["//visibility:public"],
)


# JavaScript Pyrope cfg should be deprecated slowly...
# sh_test(
#     name = "lnast_cfg_test.sh",
#     srcs = ["tests/lnast_cfg_test.sh"],
#     # tags = ["manual"],
#     # tags = ["fixme"],
#     data = [
#         "//inou/yosys:liblgraph_yosys.so",
#         "//main:lgshell",
#         ":cfg_pts"
#         ],
#     deps = [
#         "//inou/yosys:scripts",
#     ]
# )



