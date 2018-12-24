#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "headers",
    hdrs = glob(["include/*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["include"],
    deps = ["@sparsehash//:headers"]
)

cc_test(
    name = "dense_test",
    srcs = ["tests/dense_test.cpp"],
    deps = [
        ":headers",
    ],
)

cc_test(
    name = "dense_bench",
    srcs = ["tests/dense_bench.cpp"],
    deps = [
        ":headers",
        "@yas//:headers",
        "//bench:headers",
    ],
)

cc_test(
    name = "char_test",
    srcs = ["tests/char_test.cpp"],
    deps = [
        ":headers",
    ],
)

