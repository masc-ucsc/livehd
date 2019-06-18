#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

cc_binary(
    name = "lgshell",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"])
          +glob(["*_api.hpp"]),
    deps = [
            "@replxx//:replxx",
            "@mustache//:headers",
            "//elab:elab",
            "//eprp:eprp",
            "//core:core",

            "//inou/cfg:inou_cfg",
            "//inou/cgen:inou_cgen",
            "//inou/liveparse:inou_liveparse",
            # "//inou/json:inou_json",
            #"//inou/lef:inou_lef",
            "//inou/pyrope:inou_pyrope",
            "//inou/graphviz:inou_graphviz",
            # "//inou/rand:inou_rand",
            "//inou/lnast_dfg:inou_lnast_dfg",

            # "//pass/abc:pass_abc",
            "//pass/dfg:pass_dfg",
            "//pass/dce:pass_dce",
            "//pass/opentimer:pass_opentimer",
            "//pass/punch:pass_punch",
            "//pass/sample:pass_sample",
            # "//pass/bitwidth:pass_bitwidth",
            "//pass/mockturtle:pass_mockturtle",

            # "//cops/live:cops_live",

            #add dependencies to new passes here
    ],
    data = ["//inou/yosys:liblgraph_yosys.so",   # A bit special as only libraries are used in yosys -m XXX
            "inou_yosys_read.ys",
            "inou_yosys_write.ys",
    ],
    visibility = ["//visibility:public"],
    #linkopts = ["-static"],
)

sh_test(
    name = "lgshell_test.sh",
    srcs = ["tests/lgshell_test.sh"],
    data = [
        "//inou/yosys:liblgraph_yosys.so",
        "//inou/yosys:verilog_tests",
        ":lgshell",
    ],
)

sh_test(
    name = "verilog.sh",
    srcs = ["tests/verilog.sh"],
    data = [
        "//inou/yosys:liblgraph_yosys.so",
        "//inou/yosys:verilog_tests",
        ":lgshell",
        ],
    deps = [
        "//inou/yosys:scripts",
    ]
)

sh_test(
    name = "verilog.sh-long",
    srcs = ["tests/verilog.sh"],
    data = [
        "//inou/yosys:liblgraph_yosys.so",
        "//inou/yosys:verilog_tests",
        ":lgshell",
        ],
    tags = ["long2"],
    size = "large",
    args = ["long"],
    deps = [
        "//inou/yosys:scripts",
    ]
)

cc_binary(
    name = "uclient_test",
    srcs = ["uclient_test.cpp"],
    deps = ["@httplib//:headers",
    ],
)

cc_binary(
    name = "userver_test",
    srcs = ["userver_test.cpp"],
    deps = ["@httplib//:headers",
    ],
    linkopts = ["-lpthread"],
)

cc_test(
    name = "main_test",
    srcs = ["main_test.cpp"],
    deps = [
        "@gtest//:gtest_main",
    ],
    data = [
        ":lgshell",
    ],
    linkopts = ["-lutil"],
    local = 1, # Weird fork error due to forkpty otherwise
)

