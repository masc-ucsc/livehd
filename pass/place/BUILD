load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

filegroup(
    name = "place_srcs",
    srcs = [
        "color_centroids.cpp",
        "color_centroids.hpp",
        "h2d.cpp",
        "h2d.hpp",
        "memory_proxy.cpp",
        "memory_proxy.hpp",
        "place.cpp",
        "place.hpp",
        "repair.cpp",
        "repair.hpp",
        "resynthesize.cpp",
        "steiner_repair.cpp",
        "steiner_repair.hpp",
        "timing_place.cpp",
        "technology_rc.cpp",
        "technology_rc.hpp",
    ],
    visibility = ["//visibility:public"],
)

genrule(
    name = "place_salt",
    srcs = [
        ":place_srcs",
        "//:MODULE.bazel",
        "//graph",
        "//inou/lefdef:inou_lefdef",
        "//pass/liberty:liberty_dff",
        "//pass/partition:partition_srcs",
        "@dreamplace",
        "@dreamplace//:macro_place",
        "@dreamplace//:macro_hhds",
        "@dreamplace//:macro_report",
        "@dreamplace//:macro_render",
        "@dreamplace//dreamplace/native:placer_srcs",
        "@dreamplace//dreamplace/native/macro:h2d_srcs",
        "//packages:dreamplace-lemon.patch",
        "//packages:dreamplace-zero-area.patch",
        "//packages:dreamplace-instantiate.patch",
        "//packages:dreamplace-mixed-size.patch",
    ],
    outs = ["place_salt.hpp"],
    cmd = "h=$$(cat $(SRCS) | (shasum -a 256 2>/dev/null || sha256sum) | cut -c1-16); " +
          "printf '#pragma once\\n#include <cstdint>\\nnamespace livehd::placement { inline constexpr uint64_t kPlaceSalt = 0x%sULL; }\\n' \"$$h\" > $@",
)

cc_library(
    name = "place",
    srcs = [
        "color_centroids.cpp",
        "h2d.cpp",
        "memory_proxy.cpp",
        "place.cpp",
        "repair.cpp",
        "resynthesize.cpp",
        "steiner_repair.cpp",
        "timing_place.cpp",
        "technology_rc.cpp",
    ],
    hdrs = [
        "color_centroids.hpp",
        "h2d.hpp",
        "memory_proxy.hpp",
        "place.hpp",
        "repair.hpp",
        "steiner_repair.hpp",
        "technology_rc.hpp",
        ":place_salt",
    ],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [
        "//core",
        "//graph",
        "//inou/lefdef:inou_lefdef",
        "//pass/liberty:liberty_dff",
        "//pass/partition:flatten",
        "//pass/synth",
        "@dreamplace",
        "@dreamplace//:macro_hhds",
        "@dreamplace//:macro_place",
        "@dreamplace//:macro_render",
        "@dreamplace//:macro_report",
        "@dreamplace//thirdparty/flute",
        "@rapidjson",
        "@rules_cc//cc/runfiles",
    ],
)

cc_test(
    name = "place_test",
    srcs = ["place_test.cpp"],
    copts = COPTS,
    data = ["//inou/lefdef:test_data"],
    deps = [
        ":place",
        "//pass/opentimer:pass_opentimer",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "color_centroids_test",
    srcs = ["color_centroids_test.cpp"],
    copts = COPTS,
    deps = [
        ":place",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "repair_test",
    size = "small",
    srcs = ["repair_test.cpp"],
    copts = COPTS,
    data = ["//inou/lefdef:test_data"],
    deps = [
        ":place",
        "//pass/abc:pass_abc",
        "//pass/opentimer:pass_opentimer",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "technology_rc_test",
    srcs = ["technology_rc_test.cpp"],
    copts = COPTS,
    deps = [
        ":place",
        "@googletest//:gtest_main",
    ],
)
