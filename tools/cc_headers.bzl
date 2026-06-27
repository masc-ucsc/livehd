""" cc_direct_headers — re-export a cc_library's OWN headers as plain runfiles.

A `data` dependency on a `cc_library` does NOT place its headers in the
consumer's runfiles (cc_library contributes a compiled lib, not sources). A
test that compiles generated C++ at runtime (e.g. the `lhd sim` Slop drivers)
needs the dependency's headers as ordinary files it can point `-I` at.

This rule collects each dep's `direct_headers` only (its own `hdrs`), so it
yields exactly the library's public headers WITHOUT dragging in the transitive
header closure (e.g. abseil) that the generated code never includes.
"""

load("@rules_cc//cc/common:cc_info.bzl", "CcInfo")

def _cc_direct_headers_impl(ctx):
    hdrs = []
    for dep in ctx.attr.deps:
        hdrs.extend(dep[CcInfo].compilation_context.direct_headers)
    files = depset(hdrs)
    return [DefaultInfo(files = files, runfiles = ctx.runfiles(files = hdrs))]

cc_direct_headers = rule(
    implementation = _cc_direct_headers_impl,
    attrs = {
        "deps": attr.label_list(providers = [CcInfo], mandatory = True),
    },
    doc = "Expose the direct (own) headers of each cc_library dep as runfiles.",
)
