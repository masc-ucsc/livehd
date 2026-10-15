load("@rules_cc//cc:defs.bzl", "cc_library")
# Graph/network-simplex subset only. No LP frontend or GLPK dependency.
genrule(
    name = "config",
    outs = ["lemon/config.h"],
    cmd = "printf '#pragma once\\n#define LEMON_VERSION \"1.3.1\"\\n#define LEMON_HAVE_LONG_LONG 1\\n#define LEMON_USE_PTHREAD 1\\n' > $@",
)
cc_library(
    name = "graph",
    srcs = ["lemon/base.cc"],
    hdrs = glob(["lemon/**/*.h"], exclude = ["lemon/glpk.h", "lemon/lp*.h", "lemon/cbc.h", "lemon/clp.h", "lemon/cplex.h", "lemon/soplex.h"]) + [":config"],
    includes = ["."],
    visibility = ["//visibility:public"],
)
