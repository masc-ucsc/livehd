#!/usr/bin/env python3
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
"""Independent proof of generated alternative codes and CLI cache identity."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(tempfile.mkdtemp(prefix="encoding-codes-", dir=os.environ.get("TEST_TMPDIR")))
env = dict(os.environ, TEST_UNDECLARED_OUTPUTS_DIR=str(root))
subprocess.run(["pass/synth/abc_tmap_test",
                "--gtest_filter=AbcTmap.AlternativeClassEncodingPreservesEveryMappedOutput"],
               env=env, check=True, timeout=20)
witness = root / "alternative_codes.jsonl"
checker = ["python3", "pass/synth/check_witness.py"]
subprocess.run([*checker, str(witness)], check=True, timeout=10)
record = json.loads(witness.read_text())
assert record["search"]["encoding_code_limit"] == 2, record
assert record["network"]["encodings"][0]["table"]["words"] == ["00000000000000fe"], record
# Keep the original source digest intact while corrupting the total encoder.
record["network"]["encodings"][0]["table"]["words"][0] = "00000000000000ff"
corrupt = root / "corrupt.jsonl"
corrupt.write_text(json.dumps(record) + "\n")
assert subprocess.run([*checker, str(corrupt)], timeout=10).returncode == 1

source = root / "encoding.v"
source.write_text("""module encoding(input a,b,c,d,e, output y,z);
assign y = (a & ~b & ~c & d) | (~a & b & ~c & d) |
           (~a & ~b & c & d) | (a & b & c & d);
assign z = (~a & ~b & ~c) | (a & b & ~c) | (a & ~b & c) | (~a & b & c) | e;
endmodule
""")
work = root / "work"
base = [str(Path("lhd/lhd").resolve()), "synth", str(source), "--top", "encoding", "--workdir", str(work),
        "--set", "synth.mapper=synth", "--set", "synth.liberty=inou/prp/tests/abc/test.lib",
        "--set", "synth.opentimer=false", "--set", "pass.synth.max_depth=2", "--set", "pass.synth.support=3",
        "--set", "pass.synth.literals=16", "--set", "pass.synth.series=3",
        "--set", "pass.synth.cuts=1", "--set", "pass.synth.cover_limit=0", "--set", "pass.synth.divisor_limit=0",
        "--set", "pass.synth.recovery_rounds=0", "--set", "pass.synth.joint_limit=0"]
for label, limit in (("cold", 2), ("warm", 2), ("changed", 1), ("zero", 0), ("large", 4097)):
    envelope = root / (label + ".json")
    archive = root / (label + ".measurement")
    command = base + ["--set", "pass.synth.encoding_code_limit=" + str(limit),
                      "--result-json", str(envelope), "-q"]
    result = subprocess.run(["pass/synth/measure_synth", "--archive", str(archive),
                             "--seconds", "20", "--memory-mb", "4096", "--", *command],
                            capture_output=True, text=True, timeout=25)
    value = json.loads(envelope.read_text())
    if limit in (0, 4097):
        assert result.returncode != 0 and value["status"] == "fail", value
        assert "bounds" in value["error"]["message"], value
        continue
    assert result.returncode == 0 and value["status"] == "pass", (value, (archive / "command.log").read_text())
    report = value["qor"]["synth"]
    if label == "warm":
        assert report["regions_reused"] and not report["regions_searched"], report
    else:
        assert report["regions_searched"] and value["incremental"]["abc"]["misses"] > 0, value
        assert all(row["encoding_code_limit"] == limit for row in report["regions_searched"]), report
        assert any(a["encoding"]["code_queries"] > 0 and row["status"] == "unate"
                   for row in report["regions_searched"] for a in row["attempts"]), report
    archive_witness = work / "synth/qor.json.witness.jsonl"
    subprocess.run([*checker, str(archive_witness)], check=True, timeout=10)
    subprocess.run(["python3", "pass/synth/summarize.py", str(work / "synth/qor.json.synth.json"),
                    str(archive_witness), "--invocation-result", str(envelope)],
                   check=True, stdout=subprocess.DEVNULL, timeout=10)
print("alternative class-code proof and CLI cache checks passed:", root)
