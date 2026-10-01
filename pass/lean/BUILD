# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "pass_lean",
    srcs = [
        # One target, several TUs: the Eprp plugin registration lives in
        # pass_lean.cpp and must stay single, but each DIRECTION gets its own
        # translation unit so a change to one cannot reach another.
        "IR_Semantics_pass.cpp",  # Direction 2: the DesignCert certificate
        "lean_common.cpp",        # shared graph -> certificate machinery
        "pass_lean.cpp",          # Eprp surface + legacy fast-model emission
    ],
    hdrs = [
        "design_cert_export.hpp",
        "lean_common.hpp",
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

# Direction 2's certificate-sweep tooling, exposed so tests can depend on the
# scripts they pin (a regression only run by hand is not a maintained one).
filegroup(
    name = "scripts",
    srcs = glob(["scripts/*.py"]),
    visibility = ["//visibility:public"],
)
