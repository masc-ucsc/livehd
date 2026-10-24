# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

"""aeg_rules(): the build side of the aegraph rule DSL (pass/aegraph/rulegen).

One call turns `rules/<group>.rules` files into

  * `<name>_cpp`            filegroup of the generated rule code (`gen/<group>_rules.gen.cpp`,
                            with `#line` back into the .rules file) for pass_aegraph's srcs; the
                            rules register themselves through runtime/match.hpp, so nothing else
                            (registry, knobs, counters, census) needs a hand edit;
  * `aeg_fuzz_<group>_smoke`  per group: LHS/RHS equality fuzz on Dlop values (~1000 samples per
                            rule, fixed seed, LHD_AEG_FUZZ_SEED to override), the `?` refinement mode,
                            choice arms with their assignment substituted, and "the example fires";
  * `aeg_prove_<group>_smoke` per group: the bv proof tier (micro-LGraphs proven equal by
                            lec::prove_equal at widths {1,2,3,8,16,65}, <= 8 instances per rule,
                            LHD_AEG_PROVE_FULL=1 for all);
  * `<name>_md_fresh`       staleness test of the checked-in RULES.md, which is generated too;
    `<name>_refresh`        `bazel run` target that rewrites the checked-in RULES.md.

The generator is passed through `srcs` (target configuration), not `tools` (exec
configuration), for the reason tools/lhd.bzl documents: the exec configuration does not
carry the repo's -std=c++23 and would rebuild //graph a second time.

Group order is the order of `groups`: it is the tie-break after priority and file order.
Any lint error in a rule file (unknown op or pin, unbound variable, a simplify that grows
the graph, a fresh `?`, a choice arm without an assignment, an unregistered {attr}, an
unknown cost profile) fails the genrule, i.e. the build.
"""

load("@rules_cc//cc:defs.bzl", "cc_test")
load("@rules_shell//shell:sh_binary.bzl", "sh_binary")
load("@rules_shell//shell:sh_test.bzl", "sh_test")
load("//tools:copt_default.bzl", "COPTS")

def aeg_rules(
        name,
        groups,
        rulegen,
        fuzz_deps,
        prove_deps,
        analyses = "rules/analyses.decl",
        costs = [],
        native_srcs = [],
        md = "RULES.md"):
    """Generate rule code, tests and RULES.md for `rules/<group>.rules`.

    Args:
      name: prefix of the generated targets (`<name>_gen`, `<name>_cpp`, `<name>_md_fresh`).
      groups: rule group names, in tie-break order; each reads `rules/<group>.rules`.
      rulegen: label of the rulegen cc_binary.
      fuzz_deps: deps of the generated fuzz tests (the rule runtime, the test support, gtest_main).
      prove_deps: deps of the generated bv-proof tests (the same plus the LEC engine).
      analyses: the analyses.decl file.
      costs: cost profile names; each reads `cost/<name>.cost`, is linted against the rules and registered
          through `gen/cost_<name>.gen.cpp` (part of `<name>_cpp`).
      native_srcs: the hand-written rule sources, listed in RULES.md and checked for name clashes.
      md: the checked-in generated table that the staleness test compares against.
    """
    gen = name + "_gen"
    outs = []
    cpp = []
    for g in groups:
        outs += [
            "gen/%s_rules.gen.cpp" % g,
            "gen/aeg_fuzz_%s_smoke.gen.cpp" % g,
            "gen/aeg_prove_%s_smoke.gen.cpp" % g,
        ]
        cpp.append("gen/%s_rules.gen.cpp" % g)
    for c in costs:
        outs.append("gen/cost_%s.gen.cpp" % c)
        cpp.append("gen/cost_%s.gen.cpp" % c)
    outs.append("gen/RULES.generated.md")

    cmd = "$(location %s) --out-dir $(RULEDIR)/gen --analyses $(location %s)" % (rulegen, analyses)
    for g in groups:
        cmd += " --group %s=$(location rules/%s.rules)" % (g, g)
    for c in costs:
        cmd += " --cost %s=$(location cost/%s.cost)" % (c, c)
    for f in native_srcs:
        cmd += " --native $(location %s)" % f

    native.genrule(
        name = gen,
        srcs = [analyses] + ["rules/%s.rules" % g for g in groups] + ["cost/%s.cost" % c for c in costs] + native_srcs + [rulegen],
        outs = outs,
        cmd = cmd,
    )

    native.filegroup(name = name + "_cpp", srcs = cpp)

    for g in groups:
        cc_test(
            name = "aeg_fuzz_%s_smoke" % g,
            srcs = ["gen/aeg_fuzz_%s_smoke.gen.cpp" % g],
            copts = COPTS,
            deps = fuzz_deps,
        )
        cc_test(
            name = "aeg_prove_%s_smoke" % g,
            srcs = ["gen/aeg_prove_%s_smoke.gen.cpp" % g],
            copts = COPTS,
            deps = prove_deps,
        )

    sh_test(
        name = name + "_md_fresh",
        srcs = ["//tools:aegraph_md_fresh.sh"],
        args = ["$(location %s)" % md, "$(location gen/RULES.generated.md)"],
        data = [md, "gen/RULES.generated.md"],
    )

    sh_binary(
        name = name + "_refresh",
        srcs = ["//tools:aegraph_md_refresh.sh"],
        args = ["$(rootpath gen/RULES.generated.md)", native.package_name() + "/" + md],
        data = ["gen/RULES.generated.md"],
    )
