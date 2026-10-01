# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")
load("//tools:copt_default.bzl", "COPTS")

# The DesignCert exporter is deliberately free of every LGraph type, so the
# differential test below can exercise the production DCERT1 transcription
# without a graph -- and without building `lhd`.
cc_library(
    name = "design_cert_export",
    hdrs = ["design_cert_export.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
)

cc_binary(
    name = "dcert_parity",
    srcs = ["tests/dcert_parity.cpp"],
    copts = COPTS,
    deps = [":design_cert_export"],
)

cc_library(
    name = "pass_lean",
    srcs = [
        "pass_lean.cpp",
    ],
    hdrs = [
        "design_cert_export.hpp",
        "pass_lean.hpp",
    ],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//graph",
        "//pass/common:pass",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@hhds//hhds:graph",
    ],
    alwayslink = True,
)
