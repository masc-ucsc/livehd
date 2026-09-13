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

    def run(source, *flags, partial=False):
        global serial
        serial += 1
        path = root / f"case_{serial}.prp"
        path.write_text(source)
        proc = subprocess.run([LHD, "pyrope", "style", str(path), "--diag-fmt", "json", *flags],
                              text=True, capture_output=True, timeout=30)
        assert proc.returncode == 0, proc.stderr
        assert path.read_text() == source, "style must not rewrite input"
        records = [json.loads(line) for line in proc.stderr.splitlines()]
        summary = next(r for r in records if r["code"] == "style-summary")
        assert summary["attrs"]["partial"] == str(partial).lower(), proc.stderr
        findings = [r for r in records if r["code"] in ("likely-unrolled-loop", "repeated-code")]
        return findings, records, path

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

    # Shared diagnostics output, pretty rendering, metadata, and failure paths.
    output = root / "style.jsonl"
    _, _, path = run(source, "--emit", f"diagnostics:{output}")
    assert any(json.loads(s)["code"] == "likely-unrolled-loop" for s in output.read_text().splitlines())
    proc = subprocess.run([LHD, "pyrope", "style", str(path), "--diag-fmt", "pretty"], capture_output=True, text=True)
    assert proc.returncode == 0 and "3 statements per copy, repeated 4 times" in proc.stderr, proc.stderr
    assert "template:" in proc.stderr and "first copy" in proc.stderr, proc.stderr
    for args in [["describe", "pyrope style"], ["pyrope", "style", "--help"], ["help", "pyrope", "style"]]:
        proc = subprocess.run([LHD, *args, "--diag-fmt", "json"], capture_output=True, text=True)
        assert proc.returncode == 0, proc.stderr
        assert json.loads(proc.stdout)["name"] == "pyrope style", proc.stdout
    for args in [[], [str(root / "missing.prp")], [str(path), "--min-repeats", "2"],
                 [str(path), "--max-block-statements", "0"], [str(path), "--max-findings", "oops"]]:
        proc = subprocess.run([LHD, "pyrope", "style", *args], capture_output=True, text=True)
        assert proc.returncode != 0, args
    # One missing file must not prevent later inputs from being checked.
    proc = subprocess.run([LHD, "pyrope", "style", str(root / "missing.prp"), str(path), "--diag-fmt", "json"],
                          capture_output=True, text=True)
    assert proc.returncode != 0 and '"likely-unrolled-loop"' in proc.stderr, proc.stderr

print("Pyrope style checks passed")
