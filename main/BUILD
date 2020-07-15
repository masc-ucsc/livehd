#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

cc_binary(
    name = "lgshell",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"])
          +glob(["*_api.hpp"]),
    deps = [
            "@replxx//:replxx",
            "//elab:elab",
            "//eprp:eprp",
            "//core:core",

            "//inou/code_gen:inou_code_gen",
            "//inou/liveparse:inou_liveparse",
            "//inou/pyrope:inou_pyrope",
            "//inou/graphviz:inou_graphviz",
            "//inou/lnast_dfg:inou_lnast_dfg",
            "//inou/yosys:lgyosys-api",
            "//inou/firrtl:inou_firrtl_cpp",

            "//pass/common:pass",

            "//lgcpp/common:lgcpp",
            "//lgcpp/prplib:prplib",

            "//pass/sample:pass_sample",
            "//pass/punch:pass_punch",
            "//pass/mockturtle:pass_mockturtle",
            "//pass/lec:pass_lec",
            "//pass/dce:pass_dce",
            "//pass/cprop:pass_cprop",
            "//pass/lgraph_to_lnast:pass_lgraph_to_lnast",
            "//pass/bitwidth:pass_bitwidth",
            "//pass/semantic:pass_semantic",

            #add dependencies to new passes here
    ],
    data = ["//inou/yosys:liblgraph_yosys.so",   # A bit special as only libraries are used in yosys -m XXX
    ],
    visibility = ["//visibility:public"],
    #linkopts = ["-static"],
    linkstatic=False,
)

sh_test(
    name = "lgshell_test.sh",
    srcs = ["tests/lgshell_test.sh"],
    tags = ["fixme"],
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
    size = "medium", # give some extra time to the slow machines
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
    tags = ["long1"],
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

cc_library(
    name = "tmt",
    srcs = ["tmt_test.cpp"],
    hdrs = ["tmt_test.hpp"],
)

cc_test(
    name = "main_test",
    srcs = ["main_test.cpp"],
    deps = [
        "@gtest//:gtest_main",
        ":tmt",
    ],
    data = [
        ":lgshell",
    ],
    linkopts = ["-lutil"],
    local = 1, # Weird fork error due to forkpty otherwise
)

