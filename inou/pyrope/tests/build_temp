#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

cc_library(
    name="prp_test_lib",
    srcs = glob(["*.cpp"], exclude=["*test.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = ["@fmt//:fmt",
            "@iassert//:iassert",
            "@com_google_absl//absl/container:flat_hash_map",
            "@com_google_absl//absl/container:flat_hash_set",
    ],
    linkopts = ["-lpthread"],
)

cc_binary(
		name="prp_test",
		srcs=["prp_test.cpp"],
		deps= [
				":prp_test_lib",
		],
)
