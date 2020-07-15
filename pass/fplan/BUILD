#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

cc_library(
    name = "fplan",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*test*.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    includes = ["."],
    linkopts = ["-lstdc++fs"],  # Needed for gcc 7-8
    alwayslink=1,
    visibility = ["//visibility:public"],
    deps = [
         "//pass/common:pass",
         "@iassert//:iassert",
         "@rapidjson//:headers",
#        "//core",
         ],
    )

cc_binary(
	  name = "fp",
	  srcs = ["tests/fp.cpp"],
	  deps = [
		    ":fplan",
	      ],
    )

