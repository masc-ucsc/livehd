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
    alwayslink=True,
    deps = [
         "//core:core",
         "//pass/common:pass",
         "//task:task",
         "@iassert//:iassert",
         "@fmt//:fmt",
         "//third_party/misc/ArchFP:archfp",
         ],
    )
