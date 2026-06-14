# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# buildifier: disable=load
load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "upass_bitwidth",
    srcs = ["upass_bitwidth.cpp"],
    hdrs = [
        "lnast_range.hpp",
        "upass_bitwidth.hpp",
    ],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        ":wrap_sat_hdr",
        "//core",
        "//lnast",
        "//pass/common:pass",
        "//upass/core:upass",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:inlined_vector",
    ],
    alwayslink = True,  # ensures the static plugin_bitwidth registration fires
)

# Header-only utility: pure wrap/saturate narrowing math. Lives in
# upass/bitwidth/ per the LiveHD docs ("max/min-dependent operations
# live in bitwidth") but exposed as a separate target so consumers like
# upass/attributes/ can pull in the helpers without linking the
# bitwidth plugin's static registration.
cc_library(
    name = "wrap_sat_hdr",
    hdrs = ["wrap_sat.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//core",
        "//upass/core:range_bits_hdr",
    ],
)

cc_test(
    name = "upass_bitwidth_test",
    srcs = ["upass_bitwidth_test.cpp"],
    copts = COPTS,
    deps = [
        ":upass_bitwidth",
        "//core",
        "//lnast",
        "//upass/attributes:upass_attributes",  # transitive declared dep (2b/I)
        "//upass/constprop:upass_constprop",  # bitwidth's declared dep (2b/I)
        "//upass/core:upass",
        "//upass/runner:upass_runner",
        "//upass/typecheck:upass_typecheck",  # transitive declared dep (2b/I)
        "@googletest//:gtest_main",
    ],
)
