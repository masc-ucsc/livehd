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
         "//pass/common:pass",
         "@iassert//:iassert",
         "@rapidjson//:headers",
         "@graph//:graph",
         "@range-v3//:range-v3",
         ],
    )

cc_binary(
	  name = "fp",
	  srcs = ["tests/fp.cpp"],
	  deps = [
		    ":fplan",
	      ],
    )

cc_binary(
	  name = "gtest",
	  srcs = glob(["*.hpp", "tests/graph_test.cpp"]),
	  deps = [
         "@graph//:graph",
         "@range-v3//:range-v3",
         ],
    )
