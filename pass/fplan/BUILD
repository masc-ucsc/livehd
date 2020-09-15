#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

cc_library(
    name = "fplan",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*test*.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
         "//core:core",
         "//pass/common:pass",
         "//task:task",
         "//third_party/misc/blobb_compass:blobb",
         "@iassert//:iassert",
         "@rapidjson//:headers",
         "@graph//:graph",
         "@range-v3//:range-v3",
         "@fmt//:fmt",
         ],
    )
