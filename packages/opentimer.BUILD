
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "opentimer",
    hdrs = glob(["ot/*.hpp", "ot/**/*.hpp"]),
    copts = [
        "-w",
        "-O2",
    ],
    srcs = glob(["ot/**/*.cpp"]),
    includes = ["."],
    visibility = ["//visibility:public"],
)

# OpenTimer bundles its own (forked 4.0) taskflow under ot/taskflow, compiled
# into every OT object. Anything living in the same binary as OpenTimer MUST
# use THIS taskflow copy: linking a second taskflow version alongside is an
# ODR violation — two different tf::Executor/tf::Worker layouts behind one set
# of weak inline symbols — that corrupts the executor at runtime (EXC_BAD_ACCESS
# inside tf::Worker lookups the first time both run in one process). The
# `includes = ["ot"]` entry maps the canonical #include "taskflow/taskflow.hpp"
# onto the bundled ot/taskflow headers, so consumers need no source changes.
cc_library(
    name = "taskflow",
    hdrs = glob(["ot/taskflow/**/*.hpp"]),
    includes = ["ot"],
    visibility = ["//visibility:public"],
)

