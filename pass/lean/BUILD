# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "lean_graph_scan",
    srcs = ["graph_access.cpp", "design_scan.cpp"],
    hdrs = ["graph_access.hpp", "design_scan.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = ["//graph", "@abseil-cpp//absl/container:flat_hash_set", "@hhds//hhds:graph"],
)

cc_library(
    name = "lean_certificate_ir",
    srcs = ["certificate_ir.cpp", "memory_lowering.cpp"],
    hdrs = ["certificate_ir.hpp", "certificate_builder.hpp", "memory_lowering.hpp", "design_scan.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "lean_format",
    srcs = ["lean_format.cpp"],
    hdrs = ["lean_format.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [":lean_certificate_ir"],
)

cc_library(
    name = "lean_emitters",
    srcs = ["emit_design_cert.cpp"],
    hdrs = ["emit_design_cert.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [":lean_format"],
)

cc_library(
    name = "lean_legacy_schema",
    srcs = ["emit_legacy_schema.cpp"],
    hdrs = ["emit_legacy_schema.hpp"],
    copts = COPTS,
    deps = [":lean_format"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "lean_legacy_fast_model",
    srcs = ["emit_legacy_fast_model.cpp"],
    hdrs = ["emit_legacy_fast_model.hpp"],
    copts = COPTS,
    deps = [":lean_legacy_schema"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "lean_legacy_graph_cert",
    srcs = ["emit_legacy_graph_cert.cpp"],
    hdrs = ["emit_legacy_graph_cert.hpp"],
    copts = COPTS,
    deps = [":lean_legacy_fast_model"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "lean_legacy_cert_wf",
    srcs = ["emit_legacy_cert_wf.cpp"],
    hdrs = ["emit_legacy_cert_wf.hpp", "lean_options.hpp", "legacy_model.hpp"],
    copts = COPTS,
    deps = [":lean_legacy_graph_cert"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "lean_legacy_bridge",
    srcs = ["emit_legacy_fast_bridge.cpp"],
    hdrs = ["emit_legacy_fast_bridge.hpp"],
    copts = COPTS,
    deps = [":lean_legacy_fast_model"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "lean_legacy_emitter",
    srcs = ["legacy_model.cpp"],
    hdrs = ["legacy_model.hpp"],
    copts = COPTS,
    deps = [":lean_legacy_schema", ":lean_legacy_fast_model", ":lean_legacy_graph_cert", ":lean_legacy_cert_wf", ":lean_legacy_bridge"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "pass_lean",
    srcs = ["pass_lean.cpp", "lean_options.cpp"],
    hdrs = ["pass_lean.hpp", "lean_options.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        ":lean_legacy_emitter",
        ":lean_graph_scan",
        ":lean_emitters",
        "//graph",
        "//pass/common:pass",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@hhds//hhds:graph",
    ],
    alwayslink = True,
)

cc_binary(
    name = "lean_export_graph",
    srcs = ["tests/export_graph.cpp"],
    copts = COPTS,
    deps = [":pass_lean", "//graph", "//pass/common:pass"],
)

cc_binary(
    name = "lean_legacy_fixtures",
    srcs = ["tests/legacy_fixtures.cpp"],
    copts = COPTS,
    deps = ["//graph"],
)

cc_test(
    name = "certificate_ir_test",
    srcs = ["certificate_ir_test.cpp"],
    copts = COPTS,
    deps = [":lean_emitters", "@googletest//:gtest_main"],
)

cc_test(
    name = "design_scan_test",
    srcs = ["design_scan_test.cpp"],
    copts = COPTS,
    deps = [":lean_graph_scan", ":lean_emitters", "//graph", "@googletest//:gtest_main"],
)

cc_test(
    name = "legacy_model_test",
    srcs = ["legacy_model_test.cpp"],
    copts = COPTS,
    deps = [":lean_legacy_emitter", ":lean_emitters", "@googletest//:gtest_main"],
)

test_suite(
    name = "lean_export_tests",
    tests = [":certificate_ir_test", ":design_scan_test", ":legacy_model_test"],
)
