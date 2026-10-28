# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")
load("@rules_python//python:defs.bzl", "py_test")
load("//tools:copt_default.bzl", "COPTS")

py_test(
    name = "coreet_sweep_freshness_smoke",
    srcs = ["Lgraph_IR_Semantics_Simulator/tests/coreet_sweep_freshness_smoke.py"],
    args = [
        "$(location Lgraph_IR_Semantics_Simulator/scripts/coreet_sweep_freshness_controls.sh)",
        "$(location Lgraph_IR_Semantics_Simulator/scripts/coreet_full_flow.sh)",
    ],
    data = [
        "Lgraph_IR_Semantics_Simulator/scripts/coreet_sweep_freshness_controls.sh",
        "Lgraph_IR_Semantics_Simulator/scripts/coreet_full_flow.sh",
    ],
    legacy_create_init = 0,
    timeout = "short",
)

cc_library(
    name = "lean_graph_scan",
    srcs = ["Exporter/Frontend/graph_access.cpp", "Exporter/Frontend/design_scan.cpp"],
    hdrs = ["Exporter/Frontend/graph_access.hpp", "Exporter/Frontend/design_scan.hpp"],
    copts = COPTS,
    includes = ["Exporter/Frontend"],
    visibility = ["//visibility:public"],
    deps = ["//graph", "@abseil-cpp//absl/container:flat_hash_set", "@hhds//hhds:graph"],
)

cc_library(
    name = "lean_certificate_ir",
    srcs = ["Exporter/Frontend/certificate_ir.cpp", "Exporter/Frontend/memory_lowering.cpp", "Exporter/Frontend/design_io_meta.cpp"],
    hdrs = ["Exporter/Frontend/certificate_ir.hpp", "Exporter/Frontend/certificate_builder.hpp", "Exporter/Frontend/memory_lowering.hpp", "Exporter/Frontend/design_scan.hpp", "Exporter/Frontend/design_io_meta.hpp"],
    copts = COPTS,
    includes = ["Exporter/Frontend"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "lean_format",
    srcs = ["Exporter/Frontend/lean_format.cpp"],
    hdrs = ["Exporter/Frontend/lean_format.hpp"],
    copts = COPTS,
    includes = ["Exporter/Frontend"],
    visibility = ["//visibility:public"],
    deps = [":lean_certificate_ir"],
)

cc_library(
    name = "lean_dcert",
    srcs = ["Exporter/DesignCert/emit_dcert.cpp"],
    hdrs = ["Exporter/DesignCert/emit_dcert.hpp"],
    copts = COPTS,
    includes = ["Exporter/DesignCert"],
    visibility = ["//visibility:public"],
    deps = [":lean_certificate_ir"],
)

cc_library(
    name = "lean_emitters",
    srcs = ["Exporter/DesignCert/emit_design_cert.cpp"],
    hdrs = ["Exporter/DesignCert/emit_design_cert.hpp"],
    copts = COPTS,
    includes = ["Exporter/DesignCert"],
    visibility = ["//visibility:public"],
    deps = [":lean_format"],
)

cc_library(
    name = "lean_options",
    srcs = ["Exporter/Frontend/lean_options.cpp"],
    hdrs = ["Exporter/Frontend/lean_options.hpp"],
    includes = ["Exporter/Frontend"],
    copts = COPTS,
    deps = ["//pass/common:pass"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "lean_legacy_schema",
    includes = ["Legacy_Fast_Model_Simulator"],
    srcs = ["Legacy_Fast_Model_Simulator/emit_legacy_schema.cpp"],
    hdrs = ["Legacy_Fast_Model_Simulator/emit_legacy_schema.hpp"],
    copts = COPTS,
    deps = [":lean_format"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "lean_legacy_fast_model",
    includes = ["Legacy_Fast_Model_Simulator"],
    srcs = ["Legacy_Fast_Model_Simulator/emit_legacy_fast_model.cpp"],
    hdrs = ["Legacy_Fast_Model_Simulator/emit_legacy_fast_model.hpp"],
    copts = COPTS,
    deps = [":lean_legacy_schema"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "lean_legacy_graph_cert",
    includes = ["Legacy_Fast_Model_Simulator"],
    srcs = ["Legacy_Fast_Model_Simulator/emit_legacy_graph_cert.cpp"],
    hdrs = ["Legacy_Fast_Model_Simulator/emit_legacy_graph_cert.hpp"],
    copts = COPTS,
    deps = [":lean_legacy_fast_model"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "lean_legacy_cert_wf",
    includes = ["Legacy_Fast_Model_Simulator"],
    srcs = ["Legacy_Fast_Model_Simulator/emit_legacy_cert_wf.cpp"],
    hdrs = ["Legacy_Fast_Model_Simulator/emit_legacy_cert_wf.hpp", "Legacy_Fast_Model_Simulator/legacy_emit_options.hpp"],
    copts = COPTS,
    deps = [":lean_options", ":lean_legacy_graph_cert"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "lean_legacy_bridge",
    includes = ["Legacy_Fast_Model_Simulator"],
    srcs = ["Legacy_Fast_Model_Simulator/emit_legacy_fast_bridge.cpp"],
    hdrs = ["Legacy_Fast_Model_Simulator/emit_legacy_fast_bridge.hpp"],
    copts = COPTS,
    deps = [":lean_legacy_fast_model", ":lean_legacy_graph_cert"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "lean_legacy_emitter",
    includes = ["Legacy_Fast_Model_Simulator"],
    srcs = ["Legacy_Fast_Model_Simulator/legacy_model.cpp"],
    hdrs = ["Legacy_Fast_Model_Simulator/legacy_model.hpp", "Legacy_Fast_Model_Simulator/legacy_emit_options.hpp"],
    copts = COPTS,
    deps = [":lean_options", ":lean_legacy_schema", ":lean_legacy_fast_model", ":lean_legacy_graph_cert", ":lean_legacy_cert_wf", ":lean_legacy_bridge"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "pass_lean",
    srcs = ["Exporter/Frontend/pass_lean.cpp"],
    hdrs = ["Exporter/Frontend/pass_lean.hpp"],
    copts = COPTS,
    includes = ["Exporter/Frontend"],
    visibility = ["//visibility:public"],
    deps = [":lean_options", 
        ":lean_legacy_emitter",
        ":lean_graph_scan",
        ":lean_dcert",
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
    srcs = ["Exporter/Frontend/tests/export_graph.cpp"],
    copts = COPTS,
    deps = [":pass_lean", "//graph", "//pass/common:pass"],
)

cc_binary(
    name = "lean_graph_census",
    srcs = ["Exporter/Frontend/tests/graph_census.cpp"],
    copts = COPTS,
    deps = [":lean_graph_scan", "//graph"],
)

cc_binary(
    name = "lean_legacy_fixtures",
    srcs = ["tests/legacy_fixtures.cpp"],
    copts = COPTS,
    deps = ["//graph"],
)

cc_test(
    name = "certificate_ir_test",
    srcs = ["Exporter/Frontend/certificate_ir_test.cpp"],
    copts = COPTS,
    deps = [":lean_emitters", "@googletest//:gtest_main"],
)

cc_test(
    name = "design_scan_test",
    srcs = ["Exporter/Frontend/design_scan_test.cpp"],
    # Project-local scratch: /tmp is shared between parallel jobs on this box
    # and is not the configured runtime area.
    local_defines = ['LIVEHD_TEST_RUNTIME_DIR=\\"generated/pass_lean_runtime_tmp\\"'],
    copts = COPTS,
    deps = [":lean_graph_scan", ":lean_dcert", ":lean_emitters", "//graph", "@googletest//:gtest_main"],
)

cc_test(
    name = "legacy_model_test",
    srcs = ["Legacy_Fast_Model_Simulator/legacy_model_test.cpp"],
    copts = COPTS,
    deps = ["//graph", ":lean_graph_scan", ":lean_legacy_emitter", ":lean_emitters", "@googletest//:gtest_main"],
)

cc_test(
    name = "emit_dcert_test",
    srcs = ["Exporter/DesignCert/emit_dcert_test.cpp"],
    copts = COPTS,
    deps = [":lean_dcert", ":lean_emitters", "@googletest//:gtest_main"],
)

test_suite(
    name = "lean_export_tests",
    tests = [
        ":certificate_ir_test",
        ":emit_dcert_test",
        ":design_scan_test",
        ":legacy_model_test",
    ],
)
