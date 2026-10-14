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
