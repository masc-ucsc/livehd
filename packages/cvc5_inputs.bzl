# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
load("@rules_cc//cc/common:cc_info.bzl", "CcInfo")

def _header_impl(ctx):
    for header in ctx.attr.library[CcInfo].compilation_context.headers.to_list():
        if header.basename == ctx.attr.basename:
            out = ctx.actions.declare_file(ctx.attr.basename)
            ctx.actions.symlink(output = out, target_file = header)
            return [DefaultInfo(files = depset([out]))]
    fail("missing public header " + ctx.attr.basename)

cvc5_header = rule(
    implementation = _header_impl,
    attrs = {"library": attr.label(providers = [CcInfo]), "basename": attr.string(mandatory = True)},
)

def _python_impl(ctx):
    runtime = ctx.toolchains["@bazel_tools//tools/python:toolchain_type"].py3_runtime
    out = ctx.actions.declare_file(ctx.label.name + "/python")
    ctx.actions.symlink(output = out, target_file = runtime.interpreter)
    return [DefaultInfo(files = depset([out], transitive = [runtime.files])), OutputGroupInfo(interpreter = depset([out]))]

cvc5_python = rule(implementation = _python_impl, toolchains = ["@bazel_tools//tools/python:toolchain_type"])

# Keep nested solver builds and the Mach-O isolation link on the same minimum
# deployment target as Bazel's C++ toolchain, including exec configurations.
def _platform_variables_impl(ctx):
    minimum = ""
    if ctx.target_platform_has_constraint(ctx.attr._macos[platform_common.ConstraintValueInfo]):
        config = ctx.attr._xcode_config[apple_common.XcodeVersionConfig]
        minimum = str(config.minimum_os_for_platform_type(apple_common.platform_type.macos))
    return [platform_common.TemplateVariableInfo({"CVC5_MACOS_MINIMUM_OS": minimum})]

cvc5_platform_variables = rule(
    implementation = _platform_variables_impl,
    attrs = {
        "_macos": attr.label(default = Label("@platforms//os:macos")),
        "_xcode_config": attr.label(default = configuration_field(fragment = "apple", name = "xcode_config_label")),
    },
    fragments = ["apple"],
)

CVC5_MACHO_TOOLS = select({
    "@platforms//os:macos": ["@llvm-project//llvm:llvm-objcopy"],
    "//conditions:default": [],
})

def cvc5_macho_archive(prefix):
    # Renaming also covers weak definitions and tentative C globals. Simply
    # hiding the solver symbols in ld -r left prebuilt CaDiCaL subject to the
    # final archive order when ABC was linked into the same executable.
    return """
clang -nostdlib -mmacosx-version-min=$(CVC5_MACOS_MINIMUM_OS) -Wl,-r,-all_load \\
  -Wl,-unexported_symbol,'*CaDiCaL*' -Wl,-unexported_symbol,'*ccadical*' \\
  -Wl,-unexported_symbol,'*Reap*' -Wl,-unexported_symbol,'_ipasir_*' \\
  -Wl,-unexported_symbol,'_kissat_*' -Wl,-unexported_symbol,'_kitten_*' \\
  -Wl,-unexported_symbol,'_new_learned_klause*' \\
  -Wl,-unexported_symbol,'*CMSat*' -Wl,-unexported_symbol,'_cmsat_*' \\
  -o "$$work/combined.o" $(SRCS)
nm -P "$$work/combined.o" \\
  | awk '$$1 ~ /CaDiCaL|ccadical|Reap|^_ipasir_|^_kissat_|^_kitten_|^_new_learned_klause|CMSat|^_cmsat_/ && $$2 != "U" {print $$1, "%s" $$1}' \\
  | sort -u > "$$work/private.syms"
$(execpath @llvm-project//llvm:llvm-objcopy) --redefine-syms="$$work/private.syms" \\
  "$$work/combined.o" "$$work/private.o"
rm -f "$@"
ar rcs "$@" "$$work/private.o"
""" % prefix
