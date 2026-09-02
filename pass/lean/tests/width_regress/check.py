#!/usr/bin/env python3
"""Evaluate an emitted verified-compiler model and compare against expectations.

This is the regression the constant-width bug needed: it does not check that the
model is SELF-consistent (step 5 already proves that, and proved it while the
value was wrong) -- it checks the model against numbers computed independently
from the RTL's own literals.

Usage: check.py <Top>_Lgraph.lean <Top> <lake-dir>
"""

import re
import subprocess
import sys

# (input vector, {output index: expected value}) -- derived from the RTL literals
# in const_width_regress.sv, not from any pass.lean artifact.
CASES = [
    # a = 0, sh = 0
    ("#[mk_bv 32 0, mk_bv 5 0]", {
        "o_sra_pos_const": 0x6000000 >> 4,
        "o_wide_const":    0x00c000000000010,
        "o_shl_const":     (0x123 << 8) & 0xFFFFFFFF,
        "o_and_wide":      0,
        "o_big_const":     0xDEADBEEFCAFEBABE,
        "o_eq_const":      0,
    }),
    ("#[mk_bv 32 0x6000000, mk_bv 5 0]", {
        "o_and_wide": 0x6000000,
        "o_eq_const": 1,
    }),
]


def main():
    if len(sys.argv) < 4:
        print("usage: check.py <lean file> <Top> <lake dir>", file=sys.stderr)
        return 2
    path, top, lake_dir = sys.argv[1], sys.argv[2], sys.argv[3]
    src = open(path, encoding="utf-8", errors="replace").read()

    # Output order in the DesignCert is the emitter's map order; recover the
    # names from the RTL declaration order is not safe, so evaluate the whole
    # output array and match by VALUE against the expected multiset.
    probe = src + "\n"
    for vec, _ in CASES:
        probe += f"#eval ({top}_step {vec} ⟨#[], #[]⟩).outputs.map bv_uint\n"
    tmp = f"{lake_dir}/probes/_width_regress.lean"
    open(tmp, "w").write(probe)

    r = subprocess.run(["lake", "env", "lean", "probes/_width_regress.lean"],
                       cwd=lake_dir, capture_output=True, text=True, timeout=1800)
    if r.returncode != 0:
        print("FAIL: model did not elaborate")
        print(r.stdout[-2000:], r.stderr[-2000:])
        return 1

    got = [set(int(x) for x in re.findall(r"\d+", ln))
           for ln in r.stdout.splitlines() if ln.strip().startswith("#[")]
    if len(got) < len(CASES):
        print(f"FAIL: expected {len(CASES)} eval lines, got {len(got)}")
        print(r.stdout[-1500:])
        return 1

    fails = []
    for i, (vec, exp) in enumerate(CASES):
        for name, want in exp.items():
            if want not in got[i]:
                fails.append(f"case {i} ({vec}): {name} expected {want} (0x{want:x}) not among {sorted(got[i])}")
    for f in fails:
        print(f"FAIL: {f}")
    if not fails:
        print(f"PASS: {len(CASES)} cases, all expected values present")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
