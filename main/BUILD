#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

cc_binary(
    name = "lgshell",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"])
          +glob(["*_api.hpp"]),
    deps = ["@spdlog//:headers",
            "@replxx//:replxx",
            "@mustache//:headers",
            "//eprp:eprp",
            "//meta:meta",

            "//inou/cfg:inou_cfg",
            "//inou/json:inou_json",
            "//inou/lef:inou_lef",
            "//inou/pyrope:inou_pyrope",
            "//inou/graphviz:inou_graphviz",
            "//inou/rand:inou_rand",

            "//live/parse:chunkify",

            "//pass/abc:pass_abc",
            "//pass/dfg:pass_dfg",
            "//pass/dce:pass_dce",
            "//pass/sample:pass_sample",
            "//pass/bitwidth:pass_bitwidth",

            "//cops/live:cops_live",
    ],
    data = ["//inou/yosys:liblgraph_yosys.so",   # A bit special as only libraries are used in yosys -m XXX
            "inou_yosys_read.ys",
            "inou_yosys_write.ys",
    ],
    visibility = ["//visibility:public"],
    linkopts = ["-static"],
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

