#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

sh_binary(
    name = "linkso_tool",
    srcs = ["linkso.sh"],
    visibility = ["//visibility:public"],
)

load("@rules_foreign_cc//tools/build_defs:configure.bzl", "configure_make")

licenses(["notice"])  # Apache v2.0

package(
    default_visibility = [
        "//:__subpackages__",
    ],
)

exports_files([
    "bison.bzl",
    "flex.bzl",
])

configure_make(
    name = "bison",
    binaries = [
        "bison",
        "yacc",
    ],
    configure_env_vars = {
        "CC_FOR_BUILD": "$$CC$$",
    },
    lib_source = "@bison//:all",
    static_libraries = ["liby.a"],
    toolchains = ["@rules_m4//m4:current_m4_toolchain"],
)

filegroup(
    name = "bison_bin",
    srcs = [":bison"],
    output_group = "bison",
    visibility = ["//visibility:public"],
)

configure_make(
    name = "flex",
    binaries = [
        "flex",
    ],
    # This seems to be necessary (using tools_dep and weird path) because unlike
    # bison, flex needs to invoke m4 during build (whereas bison needs it only
    # during `configure`).
    lib_source = "@flex//:all",
    toolchains = ["@rules_m4//m4:current_m4_toolchain"],
)

filegroup(
    name = "flex_bin",
    srcs = [":flex"],
    output_group = "flex",
    visibility = ["//visibility:public"],
)
