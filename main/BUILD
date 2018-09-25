#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

cc_binary(
    name = "lgraph",
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
            "//inou/rand:inou_rand",

            "//pass/abc:pass_abc",
            "//pass/dfg:pass_dfg",
    ],
    data = ["//inou/yosys:liblgraph_yosys.so",   # A bit special as only libraries are used in yosys -m XXX
            "inou_yosys_read.ys",
            "inou_yosys_write.ys",
    ],
    linkopts = ["-ldl"],
)

