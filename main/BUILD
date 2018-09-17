#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

cc_binary(
    name = "lgraph",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"])
          +glob(["*_api.hpp"]),
    deps = ["@spdlog//:headers",
            "@replxx//:replxx",
            "//eprp:eprp",
            "//meta:meta",

            "//inou/rand:inou_rand",
            "//inou/cfg:inou_cfg",
            "//inou/lef:inou_lef",

            "//pass/abc:pass_abc",
    ],
    linkopts = ["-ldl"],
)

