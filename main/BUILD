#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

cc_binary(
    name = "lgshell",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"])
          +glob(["*_api.hpp"]),
    deps = [
            "@replxx//:replxx",

            "//core:core",
            "//elab:elab",
            "//eprp:eprp",

#Sakshi            "//inou/code_gen:inou_code_gen",
#Sakshi            "//inou/firrtl:inou_firrtl_cpp",
            "//inou/graphviz:inou_graphviz",
#  later     "//inou/json:inou_json",
            "//inou/liveparse:inou_liveparse",
            "//inou/pyrope:inou_pyrope",
            "//inou/slang:inou_slang",
            "//inou/yosys:lgyosys-api",

            "//lgcpp/common:lgcpp",
            "//lgcpp/prplib:prplib",

#Renau            "//pass/bitwidth:pass_bitwidth",
            "//pass/common:pass",
#Renau            "//pass/cprop:pass_cprop",
            "//pass/fplan:fplan",
#Renau       "//pass/lec:pass_lec",
#Sakshi            "//pass/lnast_fromlg:pass_lnast_fromlg",
            "//pass/lnast_tolg:pass_lnast_tolg",
            "//pass/lnastfmt:pass_lnastfmt",
            "//pass/mockturtle:pass_mockturtle",
            "//pass/punch:pass_punch",
            "//pass/sample:pass_sample",
            "//pass/semantic:pass_semantic",
            "//pass/compiler:pass_compiler",

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

