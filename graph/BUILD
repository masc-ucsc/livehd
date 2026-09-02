# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

# LiveHD-side graph helpers that ride on top of hhds::Graph:
#   - cell.{hpp,cpp}  → Ntype_op enum + cell-type metadata (sink/driver names,
#     loop_first/loop_last classification, multi-driver/sink predicates).
#     Used as the Type payload on hhds::NodeEntry.
#   - attrs.hpp       → All LiveHD per-node / per-pin attribute tags backed
#     by hhds::flat_storage (bits, pin_name, pin_delay, pin_signed, color, loc, place,
#     source, subid, const_value, lut, etc.). Replaces the dense maps that
#     used to live on Lgraph_attributes.
#   - ann_place.hpp   → Ann_place value type used by livehd::attrs::place.
#
# Consumers should depend on //graph + @hhds//hhds:graph directly.
cc_library(
    name = "graph",
    srcs = [
        "cell.cpp",
        "latch_contract.cpp",
        "graph_library_singleton.cpp",
        "inline_sub.cpp",
        "port_reach.cpp",
        "occurrence_materialize.cpp",
        "split_selfref.cpp",
        "loop_split.cpp",
    ],
    hdrs = [
        "ann_place.hpp",
        "attr_carry.hpp",
        "attrs.hpp",
        "cell.hpp",
        "graph_library_singleton.hpp",
        "latch_contract.hpp",
        "inline_sub.hpp",
        "loop_split.hpp",
        "node_util.hpp",
        "port_reach.hpp",
        "predict_abc_size.hpp",
        "occurrence_materialize.hpp",
        "split_selfref.hpp",
        "synthesis_cost.hpp",
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

[cc_test(
    name = test_name,
    srcs = [test_name + ".cpp"],
    copts = COPTS,
    deps = [
        ":graph",
        "@googletest//:gtest_main",
        "@hhds//hhds:graph",
    ],
) for test_name in [
    "attr_carry_test",
    "loop_split_test",
    "node_count_test",
    "ge_weight_smoke",
    "predict_abc_size_smoke",
    "const_pool_smoke",
    "latch_contract_test",
    "occurrence_materialize_test",
]]
