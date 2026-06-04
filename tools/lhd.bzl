# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

"""Thin Starlark ruleset over the `lhd` stateless kernel (task 1y-bazel).

The kernel is a pure function (declared inputs, config) -> (declared outputs,
exit code), so wiring it as a build action is a genrule: every input is
declared, every output is predeclared, and the action writes only into its
own --workdir scratch under the output tree.

NOTE: the lhd binary is passed through `srcs` (target configuration), not
`tools` (exec configuration), because the exec config does not carry the
repo's --cxxopt=-std=c++23 and would otherwise rebuild the whole LiveHD+yosys
tree a second time. LiveHD only builds host==target, so this is safe; flip to
`tools` + --host_cxxopt if cross-compilation ever matters.

Example:

    load("//tools:lhd.bzl", "lhd_verilog")

    lhd_verilog(
        name   = "foo_net",
        top    = "foo",
        srcs   = ["foo.v", "bar.v"],
        recipe = "O2",
        out    = "foo.gen.v",   # also writes foo.gen.v.result.json
    )
"""

_LHD = "//lhd"

def _src_locations(srcs):
    return " ".join(["$(locations %s)" % s for s in srcs])

def lhd_verilog(name, top, srcs, out, recipe = "O1", reader = "yosys", visibility = None):
    """Compile Verilog sources to optimized Verilog through the lhd kernel."""
    result = out + ".result.json"
    native.genrule(
        name = name,
        srcs = srcs + [_LHD],
        outs = [out, result],
        cmd = (
            "$(location {lhd}) compile verilog ".format(lhd = _LHD) +
            _src_locations(srcs) +
            " --top {top} --reader {reader} --recipe {recipe}".format(
                top = top,
                reader = reader,
                recipe = recipe,
            ) +
            " --workdir $(RULEDIR)/{name}.lhd_work".format(name = name) +
            " --emit verilog:$(location {out})".format(out = out) +
            " --result-json $(location {result})".format(result = result) +
            " --quiet"
        ),
        visibility = visibility,
    )

def lhd_pyrope_lnast(name, srcs, outdir, visibility = None):
    """Elaborate Pyrope sources into an `ln:` TreeArtifact (hhds Forest dir)."""
    native.genrule(
        name = name,
        srcs = srcs + [_LHD],
        outs = [outdir],
        cmd = (
            "$(location {lhd}) elaborate ".format(lhd = _LHD) +
            _src_locations(srcs) +
            " --workdir $(RULEDIR)/{name}.lhd_work".format(name = name) +
            " --emit-dir ln:$(location {outdir})".format(outdir = outdir) +
            " --quiet"
        ),
        visibility = visibility,
    )
