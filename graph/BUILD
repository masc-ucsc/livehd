# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")
load("//tools:copt_default.bzl", "COPTS")

# LiveHD-side graph helpers that ride on top of hhds::Graph:
#   - cell.{hpp,cpp}  → Ntype_op enum + cell-type metadata (sink/driver names,
#     loop_first/loop_last classification, multi-driver/sink predicates).
#     Used as the Type payload on hhds::NodeEntry.
#   - attrs.hpp       → All LiveHD per-node / per-pin attribute tags backed
#     by hhds::flat_storage (bits, pin_name, pin_delay, color, loc, place,
#     source, subid, const_value, lut, etc.). Replaces the dense maps that
#     used to live on Lgraph_attributes.
#   - ann_place.hpp   → Ann_place value type used by livehd::attrs::place.
#
# This target is independent of //lgraph (which is the legacy wrapper layer
# in flight for deletion — see docs/contracts/hhds_graph_migration_plan.md).
# Consumers should depend on //graph + @hhds//hhds:graph directly.
cc_library(
    name = "graph",
    srcs = ["cell.cpp"],
    hdrs = [
        "ann_place.hpp",
        "attrs.hpp",
        "cell.hpp",
        "node_util.hpp",
    ],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//core",
        "@hhds//hhds:graph",
    ],
    alwayslink = True,  # static-init registers attribute tags
)
