#!/usr/bin/env python3
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
"""Source-only style regressions: whole blocks, evidence, recovery, and CLI."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

LHD = os.path.abspath(os.environ.get("LHD", "lhd/lhd"))


with tempfile.TemporaryDirectory(prefix="lhd_style_", dir=os.environ.get("TEST_TMPDIR")) as work:
    root = Path(work)
    serial = 0

    def run_many(sources, *flags, partial=False, min_repeats=3, rule=None):
        """Analyze independent files together; retain per-file diagnostics/checks."""
        global serial
        paths = []
        for source in sources:
            serial += 1
            path = root / f"case_{serial}.prp"
            path.write_text(source)
            paths.append(path)
        threshold = [] if min_repeats is None else ["--min-repeats", str(min_repeats)]
        proc = subprocess.run([LHD, "pyrope", "style", *map(str, paths), "--diag-fmt", "json", *threshold, *flags],
                              text=True, capture_output=True, timeout=30)
        assert proc.returncode in (0, 2), proc.stderr
        codes_all = {"likely-unrolled-loop", "repeated-code", "whole-tuple-copy", "flattened-bundle-arguments",
                     "single-destination-conditional", "hardcoded-reset", "reset-port-type"}
        codes = {"likely-unrolled-loop", "repeated-code"}
        if rule == "all":
            codes |= {"whole-tuple-copy", "flattened-bundle-arguments", "single-destination-conditional"}
        elif rule:
            codes = {rule}
        # A file with no suggestions prints nothing (no summary), so records are
        # attributed to files by their own file reference.
        def owner(record):
            if record.get("attrs", {}).get("file"):
                return record["attrs"]["file"]
            for span in [record.get("span")] + [n.get("span") for n in record.get("notes", [])]:
                if span:
                    return span["file"]
            raise AssertionError(record)

        by_file = {str(p): [] for p in paths}
        for line in proc.stderr.splitlines():
            record = json.loads(line)
            assert owner(record) in by_file, record
            by_file[owner(record)].append(record)
        results = []
        for path, source in zip(paths, sources):
            records = by_file[str(path)]
            summaries = [r for r in records if r["code"] == "style-summary"]
            assert len(summaries) <= 1, records
            assert bool(summaries) == any(r["code"] in codes_all for r in records), records
            assert any(r["code"] == "partial-analysis" for r in records) == partial, records
            assert path.read_text() == source, "style must not rewrite input"
            results.append(([r for r in records if r["code"] in codes], records, path))
        # Exit 2 iff any file reported a suggestion (scripts test `$? -ne 0`).
        any_suggestion = any(int(r["attrs"]["total_findings"]) > 0 for _, recs, _ in results for r in recs
                             if r["code"] == "style-summary")
        assert proc.returncode == (2 if any_suggestion else 0), (proc.returncode, proc.stderr)
        return results

    def run(source, *flags, **kwargs):
        return run_many([source], *flags, **kwargs)[0]

    # The default starts reporting at seven copies; smaller explicit thresholds
    # remain supported for the detailed detection fixtures below.
    for copies in (6, 7):
        repeated = "\n".join(f"const lane{i} = data[{i}]" for i in range(copies))
        findings, _, _ = run(repeated, min_repeats=None)
        assert len(findings) == (1 if copies == 7 else 0), findings

    # Three sibling statements are one iteration, including a multi-line if.
    # The generated temporary numbers advance by two, slice offsets by eight.
    def block(i):
        return (f"  const t{2*i} = data#[{8*i}..={8*i+7}]\n"
                f"  const t{2*i+1} = t{2*i} + 1\n"
                f"  if enabled {{\n    out[{i}] = t{2*i+1}\n  }}\n")

    source = "comb f() -> () {\n" + "  // copy boundary\n".join(block(i) for i in range(4)) + "}\n"
    findings, _, _ = run(source)
    assert len(findings) == 1, findings
    f = findings[0]
    assert f["attrs"]["statements_per_copy"] == "3", f
    assert f["attrs"]["repetitions"] == "4", f
    assert f["span"]["start_line"] == 2 and f["span"]["end_line"] == 24, f
    assert "data#[" in f["attrs"]["template"] and "if enabled" in f["attrs"]["template"], f
    assert "(8 * i)" in f["attrs"]["progression"], f
    assert "(2 * i)" in f["attrs"]["progression"], f
    assert not run(source, "--max-block-statements", "2")[0]
    assert not run(source, "--min-repeats", "5")[0]

    # Decimal/hex spellings match by value; widths and fixed slice bounds stay.
    scalar = "\n".join(f"const walk_{i}__w1 = Unsigned(ptr#[0..=8] == {i if i < 64 else hex(i)})"
                       for i in range(60, 68)) + "\n"
    findings, _, _ = run(scalar)
    assert len(findings) == 1 and findings[0]["attrs"]["repetitions"] == "8", findings
    assert "ptr#[0..=8]" in findings[0]["attrs"]["template"], findings
    assert "walk_{p0}__w1" in findings[0]["attrs"]["template"], findings

    # Statement boundaries, not source lines; formatting/comments do not matter.
    semis = "const lane0 = data[0]; const lane1=data[1]; /* gap */ const lane2 = data[2]\n"
    findings, _, _ = run(semis)
    assert len(findings) == 1 and findings[0]["attrs"]["repetitions"] == "3", findings

    # Nested repeated statement bodies count as whole subtrees.
    nested = "\n".join(f"if enabled {{\n  out[{i}] = data[{i}]\n  valid[{i}] = true\n}}" for i in range(3))
    findings, _, _ = run(nested)
    assert len(findings) == 1 and findings[0]["attrs"]["statements_per_copy"] == "1", findings
    assert "valid[" in findings[0]["attrs"]["template"], findings

    # Distinct module ports cannot become a loop index without changing the IO.
    # Check inputs, outputs, ref arguments, nested bodies, and all lambda kinds.
    interface_cases = []
    for kind in ("comb", "mod", "pipe[1]", "fluid"):
        timing = "@[0]" if kind == "mod" else ""
        ports = ", ".join(f"io_foo{i}:U8" for i in range(1, 4))
        inputs = "\n".join(f"const lane{i} = io_foo{i} + {i}" for i in range(1, 4))
        outputs = "\n".join(f"io_foo{i} = data#[{i}]" for i in range(1, 4))
        interface_cases.append(f"{kind} f({ports}) -> () {{\n{inputs}\n}}\n")
        out_ports = ", ".join(f"io_foo{i}:U8{timing}" for i in range(1, 4))
        interface_cases.append(f"{kind} f(data:U8) -> ({out_ports}) {{\n{outputs}\n}}\n")
        interface_cases.append(f"{kind} f({ports}) -> () {{\nif true {{\n{inputs}\n}}\n}}\n")
    for case_source, (findings, _, _) in zip(interface_cases, run_many(interface_cases)):
        assert not findings, case_source
    ref_ports = ", ".join(f"ref io_foo{i}:U8" for i in range(1, 4))
    assert not run(f"comb f({ref_ports}) -> () {{\n{outputs}\n}}\n")[0]
    assert not run(f"comb outer({ports}) -> () {{\ncomb inner() -> () {{\n{inputs}\n}}\n}}\n")[0]

    # The same port may still be indexed; its numeric suffix stays literal.
    indexed = "\n".join(f"out2#[{i}] = io_foo1#[{i}]" for i in range(3))
    findings, _, _ = run(f"comb f(io_foo1:U8) -> (out2:U3) {{\n{indexed}\n}}\n")
    assert len(findings) == 1 and findings[0]["code"] == "likely-unrolled-loop", findings
    assert "out2#[{p0}] = io_foo1#[{p0}]" in findings[0]["attrs"]["template"], findings

    # IO names are scoped, not inferred from a naming convention or collected
    # globally. Unrelated locals, including names used in defaults, still vary.
    locals_source = "\n".join(f"const io_foo{i} = {i}" for i in range(1, 4))
    for prefix in ("", f"comb other({ports}) -> () {{}}\n"):
        findings, _, _ = run(prefix + f"comb f() -> () {{\n{locals_source}\n}}\n")
        assert len(findings) == 1, findings
    findings, _, _ = run(f"comb f(data:U8=io_foo1) -> () {{\n{locals_source}\n}}\n")
    assert len(findings) == 1, findings

    # Comparing entire lambda statements must also preserve their interfaces.
    modules = "\n".join(f"comb f{i}(io_foo{i}:U8) -> () {{}}" for i in range(1, 4))
    assert not run(modules)[0]

    # After an import, Tree-sitter may put the module body beside its lambda
    # signature. RenameTable's output adapter must still keep exact IO names.
    imported = 'const table = import("rename_table.rename_table")\n\n'
    out_ports = ", ".join(f"io_readPorts_{i}_data:U8@[]" for i in range(3))
    adapter = "\n".join(f"io_readPorts_{i}_data = data#[{8*i} ..+ 8]" for i in range(3))
    for gap in (" ", " // output adapter\n"):
        assert not run(imported + f"pub mod RenameTable(data:U24) -> ({out_ports}){gap}{{\n{adapter}\n}}\n")[0]
    assert not run(imported + f"comb f({ports}) -> () {{\n{inputs}\n}}\n")[0]
    findings, _, _ = run(imported + f"comb f(io_foo1:U8) -> (out2:U3) {{\n{indexed}\n}}\n")
    assert len(findings) == 1 and "io_foo1#[{p0}]" in findings[0]["attrs"]["template"], findings
    # Attaching a body must not leak its ports into a following module.
    findings, _, _ = run(imported + f"comb f({ports}) -> () {{\n{inputs}\n}}\n"
                        + f"comb g() -> () {{\n{locals_source}\n}}\n")
    assert len(findings) == 1 and "const io_foo{p0}" in findings[0]["attrs"]["template"], findings

    # Constant copies are lower-specificity repetition, not inferred unrolling.
    findings, _, _ = run("out = data\n" * 4)
    assert len(findings) == 1 and findings[0]["code"] == "repeated-code", findings

    findings, _, _ = run("\n".join(f"wrap out[{i}] = data[{i}]" for i in range(3)))
    assert len(findings) == 1 and findings[0]["attrs"]["template"].startswith("wrap "), findings

    # Alternating strides distinguish a two-statement block even when every
    # individual statement has the same normalized syntax shape.
    alternating = "\n".join(f"const t{10*i+j} = data[{10*i+j}]" for i in range(4) for j in (0, 2))
    findings, _, _ = run(alternating)
    assert len(findings) == 1 and findings[0]["attrs"]["statements_per_copy"] == "2", findings
    assert findings[0]["attrs"]["repetitions"] == "4", findings

    negatives = [
        "const a0 = in0 + 1\nconst a1 = in1 - 1\nconst a2 = in2 * 1\n",
        "const a0:U8 = in0\nconst a1:U9 = in1\nconst a2:U10 = in2\n",
        "const a0 = in0\nconst a1 = in1\nconst a2 = in7\n",  # inconsistent stride
        "const a0 = alpha\nconst a1 = beta\nconst a2 = gamma\n",  # distinct external names
        'const a0 = "s0"\nconst a1 = "s1"\nconst a2 = "s2"\n',
        "comb f() -> () { out[0] = data[0] }\ncomb g() -> () { out[1] = data[1] }\n"
        "comb h() -> () { out[2] = data[2] }\n",  # no joining across scopes
        "wrap out[0] = data[0]\nsat out[1] = data[1]\nwrap out[2] = data[2]\n",
    ]
    for case_source, (findings, _, _) in zip(negatives, run_many(negatives)):
        assert not findings, case_source

    # Damaged siblings break sequences, but intact scopes still get analyzed.
    broken = source + "comb broken( -> () {\n"
    findings, records, _ = run(broken, partial=True)
    assert any(f["attrs"]["statements_per_copy"] == "3" for f in findings), records
    assert any(r["code"] == "partial-analysis" for r in records), records

    # Ranking, overlap suppression, and max-findings apply per file.
    two = scalar + "const unrelated = stop\n" + "out = data\n" * 4
    findings, records, _ = run(two, "--max-findings", "1")
    assert len(findings) == 1 and findings[0]["attrs"]["repetitions"] == "8", findings
    summary = next(r for r in records if r["code"] == "style-summary")
    assert summary["attrs"]["total_findings"] == "2", summary

    # Large repeated scope should not enumerate all pairs or overlapping windows.
    large = "\n".join(f"const lane{i} = data[{i}]" for i in range(10000))
    findings, _, _ = run(large)
    assert len(findings) == 1 and findings[0]["attrs"]["repetitions"] == "10000", findings

    # New rules have their own structural thresholds, independent of repeats.
    tuple_rule = "whole-tuple-copy"
    argument_rule = "flattened-bundle-arguments"
    conditional_rule = "single-destination-conditional"
    tuple_source = "dst.ctrl.valid = src.ctrl.valid\n// boundary\ndst.ctrl.data = src.ctrl.data\n"
    findings, _, _ = run(tuple_source, "--min-repeats", "100", rule=tuple_rule)
    assert len(findings) == 1, findings
    f = findings[0]
    assert f["attrs"]["destination"] == "dst.ctrl" and f["attrs"]["source"] == "src.ctrl", f
    assert f["attrs"]["field_count"] == "2", f
    assert f["span"]["start_line"] == 1 and f["span"]["end_line"] == 3, f
    assert "complete compatible bundle" in f["hint"] and "LEC" in f["hint"], f
    # A shared bundle can contain several subbundles, or have asymmetric roots.
    for body, dst, src in [
        ("dst.a.x = src.a.x\ndst.b.y = src.b.y\n", "dst", "src"),
        ("dst.x = src.ctrl.x\ndst.y = src.ctrl.y\n", "dst", "src.ctrl"),
        ("dst . x = src . x; /* gap */ dst . y = src . y\n", "dst", "src"),
    ]:
        findings, _, _ = run(body, rule=tuple_rule)
        assert len(findings) == 1, findings
        assert findings[0]["attrs"]["destination"] == dst and findings[0]["attrs"]["source"] == src, findings
    negatives = [
        "dst.a = src.a\n",  # only one field
        "dst.a = src.a\ndst.b = src.c\n",  # renamed field
        "dst.a = src.a\ndst.b = other.b\n",  # different producer
        "dst.a = src.a\nother.b = src.b\n",  # different destination
        "dst.a = src.a\nconst separator = 0\ndst.b = src.b\n",
        "dst.a = src.a\nwrap dst.b = src.b\n",
        "dst.a = src.a\nsat dst.b = src.b\n",
        "dst.a = src.a\ndst.b += src.b\n",
        "dst.a = src.a\ndst.b = Unsigned(src.b)\n",
        "dst.a = src.a\ndst.b:U8 = src.b\n",
        "dst.a = src.a\ndst.a = src.a\n",  # duplicate
        "dst.a = src.a\ndst.a.b = src.a.b\n",  # ancestor/descendant
        "dst.a.b = src.a.b\ndst.a = src.a\n",  # reverse overlap
        "dst.a.x = src.b.x\ndst.c.y = src.c.y\n",  # broadened suffixes differ
        "dst[i].a = src[i].a\ndst[i].b = src[i].b\n",
        "dst.a = src.a[0]\ndst.b = src.b[0]\n",
        "obj.dst.a = obj.src.a\nobj.dst.b = obj.src.b\n",  # same root
        "`dst.a` = `src.a`\n`dst.b` = `src.b`\n",  # not tuple fields
        "const dst = (const a=src.a, const b=src.b)\n",  # already structured
        "if cond { dst.a = src.a } else { dst.b = src.b }\n",  # separate scopes
        "const dst = src\n",  # already a whole copy
    ]
    for case_source, (findings, _, _) in zip(negatives, run_many(negatives, rule=tuple_rule)):
        assert not findings, case_source

    flat_source = "const result = child(\n  `io_in.control.enable`=enabled,\n  `io_out.ready`=ready,\n  `io_in.data`=data)\n"
    findings, _, _ = run(flat_source, rule=argument_rule)
    assert len(findings) == 2, findings
    by_bundle = {f["attrs"]["bundle"]: f for f in findings}
    assert by_bundle["io_in"]["attrs"]["argument_count"] == "2", findings
    assert by_bundle["io_out"]["attrs"]["argument_count"] == "1", findings
    assert by_bundle["io_in"]["span"]["start_line"] == 2, findings
    assert "callee interface" in by_bundle["io_in"]["hint"], findings
    findings, _, _ = run('child(`io_in.data`=data)\nchild(`io_in.data`=next_data)\n', rule=argument_rule)
    assert len(findings) == 2, findings  # separate calls never merge
    negatives = [
        "child(io_in.data=data)\n",  # ordinary dotted argument
        "child(io_in_data=data)\n",
        "child(io_in=bundle)\n",
        "child(io_in=(const data=data))\n",
        "child(`ordinary`=data)\n",
        "child(`arbitrary. name`=data)\n",
        "child(`a..b`=data)\n",
        "child(`a.`=data)\n",
        "child(`.a`=data)\n",
        "const value = (const `io_in.data`=data)\n",  # tuple literal, not call
    ]
    for case_source, (findings, _, _) in zip(negatives, run_many(negatives, rule=argument_rule)):
        assert not findings, case_source

    conditional_source = "if select {\n out.value = a\n} elif other {\n out.value = b\n} elif last {\n out.value = c\n} else {\n out.value = d\n}\n"
    findings, _, _ = run(conditional_source, "--min-repeats", "100", rule=conditional_rule)
    assert len(findings) == 1, findings
    f = findings[0]
    assert f["attrs"]["destination"] == "out.value" and f["attrs"]["branch_count"] == "4", f
    assert f["span"]["start_line"] == 1 and f["span"]["end_line"] == 9, f
    assert "branch order" in f["hint"], f
    assert len(run("if select { out = a + 1; } else { /* gap */ out = b - 1; }\n", rule=conditional_rule)[0]) == 1
    # A `match` is always exhaustive, so no `else` is needed; arms may share one wrap/sat.
    match_source = "match op {\n == 0 { wrap r = a + b }\n == 1 { wrap r = a - b }\n}\n"
    findings, _, _ = run(match_source, rule=conditional_rule)
    assert len(findings) == 1, findings
    f = findings[0]
    assert f["attrs"]["destination"] == "r" and f["attrs"]["branch_count"] == "2", f
    assert f["span"]["start_line"] == 1 and f["span"]["end_line"] == 4, f
    assert "wrap r = match" in f["hint"] and "arm order" in f["hint"], f
    for positive in [
        "match op { == 0 { r = a } == 1 { r = b } }\n",
        "match op { == 0 { r = a } else { r = b } }\n",
        "match op { == 0 { out.v = a } == 1 { out.v = b } == 2 { out.v = c } else { out.v = d } }\n",
        "match op { == 0 { sat r = a } == 1 { sat r = b } }\n",
    ]:
        assert len(run(positive, rule=conditional_rule)[0]) == 1, positive
    match_negatives = [
        "match op { == 0 { r = a } }\n",  # one arm
        "match op { == 0 { r = a } == 1 { s = b } }\n",  # different destinations
        "match op { == 0 { wrap r = a } == 1 { r = b } }\n",  # mixed modifiers
        "match op { == 0 { wrap r = a } == 1 { sat r = b } }\n",
        "match op { == 0 { r = a; r = b } == 1 { r = c } }\n",
        "match op { == 0 { const t = a; r = t } == 1 { r = b } }\n",
        "match op { == 0 { r += a } == 1 { r += b } }\n",
        "match op { == 0 { r = child(a) } == 1 { r = b } }\n",
        "match op { == 0 { r[i] = a } == 1 { r[i] = b } }\n",
        "match const c = op; c { == 0 { r = a } == 1 { r = b } }\n",
        "r = match op { == 0 { a } == 1 { b } }\n",  # already an expression
        "wrap r = match op { == 0 { a } == 1 { b } }\n",
    ]
    for case_source, (findings, _, _) in zip(match_negatives, run_many(match_negatives, rule=conditional_rule)):
        assert not findings, case_source
    assert run("// prp-style-allow single-destination-conditional\n" + match_source, rule=conditional_rule)[0] == []
    # Hand-written resets: an input used only as a reset should be a structural
    # reset_pin (hardcoded-reset) and typed Reset (reset-port-type).
    hard, rtype = "hardcoded-reset", "reset-port-type"

    def mod(body, port="rst:U1", extra=""):
        return f"pub mod m(clk:Clock, {port}, din:U7{extra}) -> (dout:U7@[]) {{\n  reg q:U7 = nil\n  dout = q\n{body}}}\n"

    findings, _, _ = run(mod("  if rst_ni == 0 { q = 0 } else { q = din }\n", "rst_ni:U1"), rule=hard)
    assert len(findings) == 1, findings
    f = findings[0]
    assert f["attrs"]["polarity"] == "active-low" and f["attrs"]["registers"] == "q" and f["attrs"]["value"] == "0", f
    assert f["attrs"]["port_type"] == "U1" and "reset_pin=" in f["hint"] and "negreset=true" in f["hint"], f
    assert "reg q:U7:[reset_pin=" in f["hint"], f
    for cond, pol in [("rst", "active-high"), ("!rst", "active-low"), ("not rst", "active-low"), ("~rst", "active-low"),
                      ("(rst)", "active-high"), ("rst == 1", "active-high"), ("rst != 0", "active-high"),
                      ("rst != 1", "active-low"), ("rst == true", "active-high")]:
        findings, _, _ = run(mod(f"  if {cond} {{ q = 0 }} else {{ q = din }}\n"), rule=hard)
        assert len(findings) == 1 and findings[0]["attrs"]["polarity"] == pol, (cond, findings)
    # The clear may be the else arm (reset when the condition is false) or have no other arm.
    findings, _, _ = run(mod("  if rst { q = din } else { q = 0 }\n"), rule=hard)
    assert len(findings) == 1 and findings[0]["attrs"]["polarity"] == "active-low", findings
    assert len(run(mod("  if rst { q = 0 }\n"), rule=hard)[0]) == 1
    assert len(run(mod("  q = din\n  if rst { q = 0 }\n"), rule=hard)[0]) == 1
    # Several registers cleared by one `if`; the port may serve many clears.
    two_regs = "pub mod m(clk:Clock, rst:U1, din:U7) -> (dout:U7@[]) {\n  reg a:U7 = nil\n  reg b:U7 = nil\n  dout = a\n" \
               "  if rst { a = 0; b = 1 } else { a = din; b = a }\n}\n"
    findings, _, _ = run(two_regs, rule=hard)
    assert len(findings) == 1 and findings[0]["attrs"]["registers"] == "a, b", findings
    # A Reset-typed port still gets the structural suggestion but not the type one.
    assert len(run(mod("  if rst { q = 0 } else { q = din }\n", "rst:Reset"), rule=hard)[0]) == 1
    assert run(mod("  if rst { q = 0 } else { q = din }\n", "rst:Reset"), rule=rtype)[0] == []
    findings, _, _ = run(mod("  if rst { q = 0 } else { q = din }\n"), rule=rtype)
    assert len(findings) == 1 and findings[0]["attrs"]["port"] == "rst" and findings[0]["attrs"]["declared_type"] == "U1", findings
    assert len(run(mod("  if rst { q = 0 } else { q = din }\n", "rst:Bool"), rule=rtype)[0]) == 1
    # Already structural: only the type is left to improve.
    structural = "pub mod m(clk:Clock, rst:U1, din:U7) -> (dout:U7@[]) {\n  reg q:U7:[reset_pin=rst] = 0\n  dout = q\n  q = din\n}\n"
    assert run(structural, rule=hard)[0] == [] and len(run(structural, rule=rtype)[0]) == 1
    assert run(structural.replace("rst:U1", "rst:Reset"), rule=rtype)[0] == []
    hard_negatives = [
        mod("  if rst { q = 0 } else { q = din }\n  dout = q + rst\n"),        # also used as data
        mod("  if rst { q = 0 } else { q = din }\n", "rst:U8"),                 # not a 1-bit control
        mod("  if rst { q = 0 } else { q = din }\n  q = 1\n"),                  # a later write beats the clear
        mod("  if rst { q = 0 } else { q = 1 }\n"),                              # both arms constant: a data mux
        mod("  if rst { q = din + 1 } else { q = din }\n"),                      # no constant clear
        mod("  if rst { q = 0; dout = 1 } else { q = din }\n"),                  # clear arm also drives non-registers
        mod("  if rst { q = 0 } elif rst == 0 { q = din }\n"),                   # port used again in the chain
        mod("  if rst == 2 { q = 0 } else { q = din }\n"),                       # not a 0/1 test
        mod("  if rst { q = 0 } else { q = din }\n  if rst { q = 1 }\n  q = din\n"),
        mod("  if din == 0 { q = 0 } else { q = din }\n"),                       # a data input, not 1 bit port
        mod("  if rst and din { q = 0 } else { q = din }\n"),
        mod("  wire w = 0\n  if rst { w = 0 } else { w = 1 }\n"),             # not a register
        "pub mod m(clk:Clock, rst:U1, din:U7) -> (o:U7@[]) {\n  reg a:[4]U7 = nil\n  o = a[0]\n  if rst { a = 0 } else { a[0] = din }\n}\n",  # memory
        mod("  reg l:U7:[latch=true] = nil\n  if rst { l = 0 } else { l = din }\n"),  # latch
        mod("  reg k:U7:[reset_pin=other] = 0\n  if rst { k = 0 } else { k = din }\n", extra=", other:Reset"),  # already has a reset_pin
        mod("  reg k:U7 = 0\n  if rst { k = 0 } else { k = din }\n"),  # initial value binds the implicit reset
        "pub comb f(rst:U1, din:U7) -> (o:U7) {\n  if rst { o = 0 } else { o = din }\n}\n",
    ]
    for case_source, (findings, _, _) in zip(hard_negatives, run_many(hard_negatives, rule=hard)):
        assert findings == [], (case_source, findings)
    for case_source, (findings, _, _) in zip(hard_negatives[:1], run_many(hard_negatives[:1], rule=rtype)):
        assert findings == [], (case_source, findings)
    # Untyped parameters have no type node (regression: null-node field lookup crashed).
    assert run("pub mod m(clk:Clock, rst, din) -> (dout) {\n  reg q = nil\n  dout = q\n  if rst { q = 0 } else { q = din }\n}\n"
               "comb dox(a) -> (foo, c) { foo = (bar = a + 1) }\n", rule=hard)[0] == []
    # Both rules honor prp-style-allow (the port's rule from before the module).
    allowed = "// prp-style-allow hardcoded-reset, reset-port-type\n" + mod("  if rst { q = 0 } else { q = din }\n")
    assert run(allowed, rule=hard)[0] == [] and run(allowed, rule=rtype)[0] == []

    # `// prp-style-allow CODE[, CODE]` silences those codes for the rest of the
    # enclosing scope only; leaving the scope (tree pop) ends the allow.
    two_ifs = "if a { out = x } else { out = y }\n"
    allow = "// prp-style-allow single-destination-conditional\n"
    assert len(run(two_ifs, rule=conditional_rule)[0]) == 1
    assert run(allow + two_ifs, rule=conditional_rule)[0] == []  # file scope, rest of file
    assert run("/* prp-style-allow other-code, single-destination-conditional */\n" + two_ifs, rule=conditional_rule)[0] == []
    assert len(run("// prp-style-allow other-code\n" + two_ifs, rule=conditional_rule)[0]) == 1  # code must match
    assert len(run("// prp-style-allowed single-destination-conditional\n" + two_ifs, rule=conditional_rule)[0]) == 1
    assert len(run("// prp-style-allow\n" + two_ifs, rule=conditional_rule)[0]) == 1  # no codes
    assert len(run(two_ifs + allow, rule=conditional_rule)[0]) == 1  # only what follows the comment
    nested = "pub comb f(a:U1) -> (out:U1) {\n  if a {\n    " + allow + "    " + two_ifs + "  } else { out = 1 }\n  " + two_ifs + "}\n"
    assert [f["span"]["start_line"] for f in run(nested, rule=conditional_rule)[0]] == [6], nested  # inner arm allowed, popped after it
    # A clean file prints no summary at all (scripts treat any output as a finding).
    proc = subprocess.run([LHD, "pyrope", "style", str(run(allow + two_ifs)[2])], capture_output=True, text=True)
    assert proc.returncode == 0 and proc.stderr == "", (proc.returncode, proc.stderr)
    negatives = [
        "if enabled { out = data }\n",  # enable/hold, not exhaustive
        "if a { out = x } elif b { out = y }\n",
        "if a { out = x } else {}\n",
        "if a { out = x } else { other = y }\n",
        "if a { out = x; out = y } else { out = z }\n",
        "if a { const tmp = x; out = tmp } else { out = y }\n",
        "if a { const out = x } else { const out = y }\n",
        "if a { out += x } else { out += y }\n",
        "if a { wrap out = x } else { wrap out = y }\n",
        "if a { wrap out = x } else { sat out = y }\n",
        "if a { out[i] = x } else { out[i] = y }\n",
        "if a { out:U8 = x } else { out:U8 = y }\n",
        "if a { out = child(x) } else { out = y }\n",
        "if a { out = child(x).value } else { out = y }\n",
        "if a { out = child::[name=instance](x).value } else { out = y }\n",
        "if a { out = if b { x } else { y } } else { out = z }\n",
        "unique if a { out = x } else { out = y }\n",
        "if const c = a; c { out = x } else { out = y }\n",
        "if a { out = x } elif const c = b; c { out = y } else { out = z }\n",
        "out = if a { x } else { y }\n",  # already an expression
    ]
    for case_source, (findings, _, _) in zip(negatives, run_many(negatives, rule=conditional_rule)):
        assert not findings, case_source

    # Intact scopes remain useful beside damaged input. Evidence includes
    # related source locations, and no new rule synthesizes repetition attrs.
    combined = tuple_source + flat_source + conditional_source
    findings, records, _ = run(combined + "comb broken( -> () {\n", rule="all", partial=True)
    assert {f["code"] for f in findings} >= {tuple_rule, argument_rule, conditional_rule}, records
    for f in findings:
        if f["code"] in (tuple_rule, argument_rule, conditional_rule):
            assert "repetitions" not in f["attrs"], f
            assert f.get("notes") and all("span" in note for note in f["notes"]), f
    # A malformed region itself must not produce a new suggestion.
    for body, rule in [
        ("dst.a = src.a\ndst.b = src.b +\n", tuple_rule),
        ("child(`io_in.data`=)\n", argument_rule),
        ("if a { out = } else { out = y }\n", conditional_rule),
    ]:
        assert not run(body, rule=rule, partial=True)[0], body

    # Large structural runs retain accurate counts but bounded evidence.
    many_copies = "".join(f"dst.f{i} = src.f{i}\n" for i in range(10000))
    findings, _, _ = run(many_copies, rule=tuple_rule)
    assert len(findings) == 1 and findings[0]["attrs"]["field_count"] == "10000", findings
    assert len(findings[0]["notes"]) == 8, findings
    many_arguments = "child(" + ",".join(f"`io_in.f{i}`=data" for i in range(100)) + ")\n"
    findings, _, _ = run(many_arguments, rule=argument_rule)
    assert len(findings) == 1 and findings[0]["attrs"]["argument_count"] == "100", findings
    assert len(findings[0]["notes"]) == 8, findings

    # Distinct rules can report the same source region. Limiting the output
    # retains the same total count, and ties/order remain deterministic.
    overlapping = "".join(f"dst.f{i} = src.f{i}\n" for i in range(8))
    findings, _, _ = run(overlapping, rule="all")
    assert {f["code"] for f in findings} == {tuple_rule, "likely-unrolled-loop"}, findings
    all_findings, records, _ = run(combined, rule="all")
    limited, limited_records, _ = run(combined, "--max-findings", "1", rule="all")
    assert len(limited) == 1 and limited[0]["code"] == all_findings[0]["code"], limited
    summary = next(r for r in limited_records if r["code"] == "style-summary")
    assert int(summary["attrs"]["total_findings"]) == len(all_findings), summary
    again, _, _ = run(combined, rule="all")
    assert [(f["code"], f["span"]["start_line"]) for f in again] == [(f["code"], f["span"]["start_line"]) for f in all_findings]
    _, _, path = run(combined, rule="all")
    proc = subprocess.run([LHD, "pyrope", "style", str(path), "--diag-fmt", "pretty"], capture_output=True, text=True)
    assert proc.returncode == 2, proc.stderr
    for message in ("matching field copies", "flattened named arguments", "exhaustive branches"):
        assert message in proc.stderr, proc.stderr

    # Shared diagnostics output, pretty rendering, metadata, and failure paths.
    output = root / "style.jsonl"
    _, _, path = run(source, "--emit", f"diagnostics:{output}")
    assert any(json.loads(s)["code"] == "likely-unrolled-loop" for s in output.read_text().splitlines())
    proc = subprocess.run([LHD, "pyrope", "style", str(path), "--min-repeats", "3", "--diag-fmt", "pretty"], capture_output=True, text=True)
    assert proc.returncode == 2 and "3 statements per copy, repeated 4 times" in proc.stderr, proc.stderr
    assert "template:" in proc.stderr and "first copy" in proc.stderr, proc.stderr
    for args in [["describe", "pyrope style"], ["pyrope", "style", "--help"], ["help", "pyrope", "style"]]:
        proc = subprocess.run([LHD, *args, "--diag-fmt", "json"], capture_output=True, text=True)
        assert proc.returncode == 0, proc.stderr
        metadata = json.loads(proc.stdout)
        assert metadata["name"] == "pyrope style", proc.stdout
        for description in ("whole-tuple copy", "flattened bundle arguments", "single-destination conditionals"):
            assert description in metadata["description"], proc.stdout
        threshold = next(arg for arg in metadata["args"]["optional"] if arg["name"] == "min-repeats")
        assert threshold["default"] == 7 and threshold["min"] == 3, threshold
    for args in [[], [str(root / "missing.prp")], [str(path), "--min-repeats", "2"],
                 [str(path), "--max-block-statements", "0"], [str(path), "--max-findings", "oops"]]:
        proc = subprocess.run([LHD, "pyrope", "style", *args], capture_output=True, text=True)
        assert proc.returncode != 0, args
    # One missing file must not prevent later inputs from being checked.
    proc = subprocess.run([LHD, "pyrope", "style", str(root / "missing.prp"), str(path), "--min-repeats", "3", "--diag-fmt", "json"],
                          capture_output=True, text=True)
    assert proc.returncode == 1 and '"likely-unrolled-loop"' in proc.stderr, proc.stderr

print("Pyrope style checks passed")
