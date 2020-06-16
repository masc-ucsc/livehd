#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "pass_lgraph_to_lnast",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
      "//core:core",
    ]
)

sh_test(
    name = "lgtoln_verif_from_verilog.sh",
    srcs = ["tests/lgtoln_verif_from_verilog.sh"],
    data = [
        "//inou/yosys:liblgraph_yosys.so",
        "//inou/yosys:verilog_tests",
        "//main:lgshell",
    ],
    deps = [
        "//inou/yosys:scripts",
    ]
)

sh_test(
    name = "lgtoln_verif_from_pyrope.sh",
    srcs = ["tests/lgtoln_verif_from_pyrope.sh"],
    data = [
        "//inou/yosys:liblgraph_yosys.so",
        "//inou/cfg:cfg_pts",
        "//main:lgshell",
    ],
    deps = [
        "//inou/yosys:scripts",
    ]
)
