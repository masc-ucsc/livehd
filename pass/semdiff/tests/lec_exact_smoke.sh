#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# A rolled loop's feedback can leave signature compare points unknown even
# between identical inputs. LEC must try semdiff's exact proof before SMT,
# while still rejecting changes inside the loop or to its iteration domain.
set -eu
LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then LHD=./lhd/lhd; fi

python3 - "$LHD" <<'PY'
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

lhd = str(Path(sys.argv[1]).resolve())
source = Path("inou/prp/tests/equiv/loop_lru_bit_update.prp").read_text()
rom = """pub comb top(addr:U2) -> (q:U8) {
  const table:[4]U8 = [11, 23, 47, 89]
  q = table[addr]
}
"""
with tempfile.TemporaryDirectory(prefix="lec_exact_", dir=os.environ.get("TEST_TMPDIR")) as tmp:
    root = Path(tmp)
    variants = {
        "ref": source,
        "same": source,
        "body": source.replace("state#[i*N + j] = 0", "state#[i*N + j] = 1"),
        "range": source.replace("for j in 0..<N {", "for j in 0..<(N-1) {"),
        "rom_ref": rom,
        "rom_same": rom,
        "rom_changed": rom.replace("47", "48"),
    }
    for variant, text in variants.items():
        if variant in ("body", "range"):
            assert text != source, f"{variant}: mutation did not apply"
        folder = root / variant
        folder.mkdir()
        (folder / "loop_lru_bit_update.prp").write_text(text)

    for ref, variant, order in (("ref", "same", "top_down"), ("ref", "same", "bottom_up"),
                               ("ref", "body", "top_down"), ("ref", "range", "top_down"),
                               ("rom_ref", "rom_same", "top_down"), ("rom_ref", "rom_changed", "top_down")):
        label = f"{variant}_{order}"
        result = root / f"{label}.json"
        cmd = [lhd, "lec", "--ref", str(root / ref / "loop_lru_bit_update.prp"),
               "--impl", str(root / variant / "loop_lru_bit_update.prp"), "--top", "top",
               "--set", "formal.effort=proof", "--set", "formal.simfail=false",
               "--set", "lhd.incremental=false", "--set", f"formal.lec.hier_order={order}",
               "--workdir", str(root / f"work_{label}"), "--stats", "--result-json", str(result)]
        run = subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        assert result.is_file(), f"{label}: no result\n{run.stdout}"
        verdict = json.loads(result.read_text())["lec"]["verdict"]
        identical = variant in ("same", "rom_same")
        expected = (0, "proven-full") if identical else (10, "refuted-cex")
        assert (run.returncode, verdict) == expected, f"{label}: {verdict}\n{run.stdout}"
        if identical:
            if variant == "same":
                assert "semdiff exact traversal, no solver" in run.stdout, run.stdout
            assert re.search(r"lec\[stats\]: cvc5\s+0 solver\(s\), 0 check\(s\)", run.stdout), run.stdout
            assert "0 via solver" in run.stdout, run.stdout
        print(f"PASS: {label}: {verdict}")
PY
