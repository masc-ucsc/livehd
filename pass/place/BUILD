load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")
filegroup(name = "place_srcs", srcs = ["place.cpp", "place.hpp", "memory_proxy.cpp", "memory_proxy.hpp", "repair.cpp", "repair.hpp", "steiner_repair.hpp", "timing_place.cpp", "resynthesize.cpp", "steiner_repair.cpp"], visibility = ["//visibility:public"])
genrule(
    name = "place_salt",
    srcs = [":place_srcs", "//:MODULE.bazel", "//graph", "//inou/lefdef:inou_lefdef", "//pass/partition:partition_srcs", "@dreamplace//:dreamplace", "//packages:dreamplace-lemon.patch", "//packages:dreamplace-zero-area.patch", "//packages:dreamplace-instantiate.patch"],
    outs = ["place_salt.hpp"],
    cmd = "h=$$(cat $(SRCS) | (shasum -a 256 2>/dev/null || sha256sum) | cut -c1-16); " +
          "printf '#pragma once\\n#include <cstdint>\\nnamespace livehd::placement { inline constexpr uint64_t kPlaceSalt = 0x%sULL; }\\n' \"$$h\" > $@",
)
cc_library(
    name = "place",
    srcs = ["place.cpp", "memory_proxy.cpp", "repair.cpp", "timing_place.cpp", "resynthesize.cpp", "steiner_repair.cpp"],
    hdrs = ["place.hpp", "memory_proxy.hpp", "repair.hpp", "steiner_repair.hpp", ":place_salt"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = ["//pass/synth", "//core", "//graph", "//pass/liberty:liberty_dff", "//inou/lefdef:inou_lefdef", "//pass/partition:flatten", "@dreamplace//:dreamplace", "@dreamplace//thirdparty/flute:flute", "@rapidjson", "@rules_cc//cc/runfiles"],
)
cc_test(
    name = "place_test",
    srcs = ["place_test.cpp"],
    copts = COPTS,
    data = ["//inou/lefdef:test_data"],
    deps = [":place", "//pass/opentimer:pass_opentimer", "@googletest//:gtest_main"],
)
cc_test(
    name = "repair_test",
    size = "small",
    srcs = ["repair_test.cpp"],
    copts = COPTS,
    data = ["//inou/lefdef:test_data"],
    deps = ["//pass/abc:pass_abc", ":place", "//pass/opentimer:pass_opentimer", "@googletest//:gtest_main"],
)
