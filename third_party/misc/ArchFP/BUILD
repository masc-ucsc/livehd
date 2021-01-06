#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

cc_library(
    name = "archfp",
    srcs = glob(
        ["*.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    includes = ["."],
    visibility = ["//visibility:public"],
    copts = ["-O3", "-Wno-error"],
    )

# makefile and other non-source files are kept but not used.