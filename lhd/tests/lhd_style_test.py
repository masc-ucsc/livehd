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

    def run(source, *flags, partial=False, min_repeats=3, rule=None):
        global serial
        serial += 1
        path = root / f"case_{serial}.prp"
        path.write_text(source)
        threshold = [] if min_repeats is None else ["--min-repeats", str(min_repeats)]
        proc = subprocess.run([LHD, "pyrope", "style", str(path), "--diag-fmt", "json", *threshold, *flags],
                              text=True, capture_output=True, timeout=30)
        assert proc.returncode == 0, proc.stderr
        assert path.read_text() == source, "style must not rewrite input"
        records = [json.loads(line) for line in proc.stderr.splitlines()]
        summary = next(r for r in records if r["code"] == "style-summary")
        assert summary["attrs"]["partial"] == str(partial).lower(), proc.stderr
        codes = {"likely-unrolled-loop", "repeated-code"}
        if rule == "all":
            codes |= {"whole-tuple-copy", "flattened-bundle-arguments", "single-destination-conditional"}
        elif rule:
            codes = {rule}
        findings = [r for r in records if r["code"] in codes]
        return findings, records, path

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
    scalar = "\n".join(f"const walk_{i}__w1 = unsigned(ptr#[0..=8] == {i if i < 64 else hex(i)})"
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
    for kind in ("comb", "mod", "pipe[1]", "fluid"):
        timing = "@[0]" if kind == "mod" else ""
        ports = ", ".join(f"io_foo{i}:u8" for i in range(1, 4))
        inputs = "\n".join(f"const lane{i} = io_foo{i} + {i}" for i in range(1, 4))
        outputs = "\n".join(f"io_foo{i} = data#[{i}]" for i in range(1, 4))
        assert not run(f"{kind} f({ports}) -> () {{\n{inputs}\n}}\n")[0]
        out_ports = ", ".join(f"io_foo{i}:u8{timing}" for i in range(1, 4))
        assert not run(f"{kind} f(data:u8) -> ({out_ports}) {{\n{outputs}\n}}\n")[0]
        assert not run(f"{kind} f({ports}) -> () {{\nif true {{\n{inputs}\n}}\n}}\n")[0]
    ref_ports = ", ".join(f"ref io_foo{i}:u8" for i in range(1, 4))
    assert not run(f"comb f({ref_ports}) -> () {{\n{outputs}\n}}\n")[0]
    assert not run(f"comb outer({ports}) -> () {{\ncomb inner() -> () {{\n{inputs}\n}}\n}}\n")[0]

    # The same port may still be indexed; its numeric suffix stays literal.
    indexed = "\n".join(f"out2#[{i}] = io_foo1#[{i}]" for i in range(3))
    findings, _, _ = run(f"comb f(io_foo1:u8) -> (out2:u3) {{\n{indexed}\n}}\n")
    assert len(findings) == 1 and findings[0]["code"] == "likely-unrolled-loop", findings
    assert "out2#[{p0}] = io_foo1#[{p0}]" in findings[0]["attrs"]["template"], findings

    # IO names are scoped, not inferred from a naming convention or collected
    # globally. Unrelated locals, including names used in defaults, still vary.
    locals_source = "\n".join(f"const io_foo{i} = {i}" for i in range(1, 4))
    for prefix in ("", f"comb other({ports}) -> () {{}}\n"):
        findings, _, _ = run(prefix + f"comb f() -> () {{\n{locals_source}\n}}\n")
        assert len(findings) == 1, findings
    findings, _, _ = run(f"comb f(data:u8=io_foo1) -> () {{\n{locals_source}\n}}\n")
    assert len(findings) == 1, findings

    # Comparing entire lambda statements must also preserve their interfaces.
    modules = "\n".join(f"comb f{i}(io_foo{i}:u8) -> () {{}}" for i in range(1, 4))
    assert not run(modules)[0]

    # After an import, Tree-sitter may put the module body beside its lambda
    # signature. RenameTable's output adapter must still keep exact IO names.
    imported = 'const table = import("rename_table.rename_table")\n\n'
    out_ports = ", ".join(f"io_readPorts_{i}_data:u8@[]" for i in range(3))
    adapter = "\n".join(f"io_readPorts_{i}_data = data#[{8*i} ..+ 8]" for i in range(3))
    for gap in (" ", " // output adapter\n"):
        assert not run(imported + f"pub mod RenameTable(data:u24) -> ({out_ports}){gap}{{\n{adapter}\n}}\n")[0]
    assert not run(imported + f"comb f({ports}) -> () {{\n{inputs}\n}}\n")[0]
    findings, _, _ = run(imported + f"comb f(io_foo1:u8) -> (out2:u3) {{\n{indexed}\n}}\n")
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

    for negative in [
        "const a0 = in0 + 1\nconst a1 = in1 - 1\nconst a2 = in2 * 1\n",
        "const a0:u8 = in0\nconst a1:u9 = in1\nconst a2:u10 = in2\n",
        "const a0 = in0\nconst a1 = in1\nconst a2 = in7\n",  # inconsistent stride
        "const a0 = alpha\nconst a1 = beta\nconst a2 = gamma\n",  # distinct external names
        'const a0 = "s0"\nconst a1 = "s1"\nconst a2 = "s2"\n',
        "comb f() -> () { out[0] = data[0] }\ncomb g() -> () { out[1] = data[1] }\n"
        "comb h() -> () { out[2] = data[2] }\n",  # no joining across scopes
        "wrap out[0] = data[0]\nsat out[1] = data[1]\nwrap out[2] = data[2]\n",
    ]:
        assert not run(negative)[0], negative

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
    for body in [
        "dst.a = src.a\n",  # only one field
        "dst.a = src.a\ndst.b = src.c\n",  # renamed field
        "dst.a = src.a\ndst.b = other.b\n",  # different producer
        "dst.a = src.a\nother.b = src.b\n",  # different destination
        "dst.a = src.a\nconst separator = 0\ndst.b = src.b\n",
        "dst.a = src.a\nwrap dst.b = src.b\n",
        "dst.a = src.a\nsat dst.b = src.b\n",
        "dst.a = src.a\ndst.b += src.b\n",
        "dst.a = src.a\ndst.b = unsigned(src.b)\n",
        "dst.a = src.a\ndst.b:u8 = src.b\n",
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
    ]:
        assert not run(body, rule=tuple_rule)[0], body

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
    for body in [
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
    ]:
        assert not run(body, rule=argument_rule)[0], body

    conditional_source = "if select {\n out.value = a\n} elif other {\n out.value = b\n} elif last {\n out.value = c\n} else {\n out.value = d\n}\n"
    findings, _, _ = run(conditional_source, "--min-repeats", "100", rule=conditional_rule)
    assert len(findings) == 1, findings
    f = findings[0]
    assert f["attrs"]["destination"] == "out.value" and f["attrs"]["branch_count"] == "4", f
    assert f["span"]["start_line"] == 1 and f["span"]["end_line"] == 9, f
    assert "branch order" in f["hint"], f
    assert len(run("if select { out = a + 1; } else { /* gap */ out = b - 1; }\n", rule=conditional_rule)[0]) == 1
    for body in [
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
        "if a { out:u8 = x } else { out:u8 = y }\n",
        "if a { out = child(x) } else { out = y }\n",
        "if a { out = child(x).value } else { out = y }\n",
        "if a { out = child::[name=instance](x).value } else { out = y }\n",
        "if a { out = if b { x } else { y } } else { out = z }\n",
        "unique if a { out = x } else { out = y }\n",
        "if const c = a; c { out = x } else { out = y }\n",
        "if a { out = x } elif const c = b; c { out = y } else { out = z }\n",
        "out = if a { x } else { y }\n",  # already an expression
    ]:
        assert not run(body, rule=conditional_rule)[0], body

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
    assert proc.returncode == 0, proc.stderr
    for message in ("matching field copies", "flattened named arguments", "exhaustive branches"):
        assert message in proc.stderr, proc.stderr

    # Shared diagnostics output, pretty rendering, metadata, and failure paths.
    output = root / "style.jsonl"
    _, _, path = run(source, "--emit", f"diagnostics:{output}")
    assert any(json.loads(s)["code"] == "likely-unrolled-loop" for s in output.read_text().splitlines())
    proc = subprocess.run([LHD, "pyrope", "style", str(path), "--min-repeats", "3", "--diag-fmt", "pretty"], capture_output=True, text=True)
    assert proc.returncode == 0 and "3 statements per copy, repeated 4 times" in proc.stderr, proc.stderr
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
    assert proc.returncode != 0 and '"likely-unrolled-loop"' in proc.stderr, proc.stderr

print("Pyrope style checks passed")
