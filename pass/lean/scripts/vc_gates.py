#!/usr/bin/env python3
"""Static gates for `formal.lean.mode=verified_compiler` output.

The legacy gates (`op_census.py`, `const_parity.py`) read a shape this mode does
not produce: there are no per-node fast bodies, no `nodes_of_list`/`BT.nd` tree,
and no `_refines_fast` theorems.  A verified_compiler file is a `DesignCert`
literal plus four declarations, and the interesting failure modes are different.

Each gate below cost real time to learn:

  maxRecDepth     Without `set_option maxRecDepth`, the thousands-element array
                  literal exhausts the default depth while ELABORATING; the
                  DesignCert becomes noncomputable and every later declaration
                  fails in a way that reads like a proof failure.  Cost the first
                  DINO run: three designs x ~25 s, all misdiagnosed.

  residual-in-    A theorem whose STATEMENT names a ResidualProgram makes the
  theorem         kernel decide `.ok <Top>_residual` defeq
                  `.ok (match compileDesign D ...)`.  The kernel reduces
                  `compileDesign`, and `Array.push` is `<as.toList ++ [a]>`, so N
                  bindings cost O(N^2) kernel terms.  Measured on SingleCycleCPU:
                  OOM at 27 min / 27 GB under a 40 GB cap; 120 GB uncapped before
                  being killed.  Against 38 s / 7.4 GB for the correct shape.
                  This gate is a grep.  It runs in milliseconds.

Usage: vc_gates.py <generated.lean> <Top> [--quiet]
Exit 0 iff every gate passes.
"""

import re
import sys


def main() -> int:
    if len(sys.argv) < 3:
        print("usage: vc_gates.py <generated.lean> <Top> [--quiet]", file=sys.stderr)
        return 2
    path, top = sys.argv[1], sys.argv[2]
    quiet = "--quiet" in sys.argv
    try:
        lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
    except OSError as e:
        print(f"FAIL: cannot read {path}: {e}")
        return 1
    text = "\n".join(lines)
    fails = []

    def say(msg):
        if not quiet:
            print(msg)

    # --- shape -------------------------------------------------------------
    n_src = len(re.findall(r"SourceDesc\.", text))
    n_nod = len(re.findall(r"origin :=", text))
    say(f"sources={n_src} nodes={n_nod}")
    # A design whose outputs are ALL constant-driven has zero computed nodes and
    # is perfectly valid -- `null_vpu` and `minion_dcache_texsend` are exactly
    # that, and both prove.  What is not valid is an EMPTY certificate.
    if n_nod == 0 and n_src == 0:
        fails.append("empty certificate: no sources and no nodes")

    # --- required declarations --------------------------------------------
    for d in ("_designCert", "_step", "_compiles", "_step_correct"):
        if f"{top}{d}" not in text:
            fails.append(f"missing {top}{d}")

    # --- elaboration options ----------------------------------------------
    if "set_option maxRecDepth" not in text:
        fails.append("no `set_option maxRecDepth` (the literal will exhaust the default)")
    if "set_option maxHeartbeats" not in text:
        fails.append("no `set_option maxHeartbeats`")

    # --- the expensive one, caught by a grep ------------------------------
    # Only THEOREM statements matter: a `def <Top>_residual` is fine and is
    # emitted deliberately for `#eval`.
    for i, ln in enumerate(lines, 1):
        if re.match(r"\s*theorem\b", ln) and "_residual" in ln:
            fails.append(f"line {i}: theorem statement names a ResidualProgram "
                         f"(kernel-defeq blowup): {ln.strip()[:80]}")
    # A multi-line theorem signature: scan the statement up to `:=`.
    for m in re.finditer(r"^\s*theorem\s+\S+[^\n]*\n((?:\s+[^\n]*\n)*?)\s*:?=", text, re.M):
        if "_residual" in m.group(1):
            fails.append("a multi-line theorem statement names a ResidualProgram "
                         "(kernel-defeq blowup)")

    # --- constants must fit their declared width ---------------------------
    # The failure this catches shipped and passed BOTH other gates: the fast
    # model and the certificate share `pin_width`, so an unsized constant
    # modeled at width 1 truncated IDENTICALLY on both sides and step 5 proved;
    # the LEC gate compares RTL to LGraph and never reads the certificate.
    # graph/node_util.hpp:323 calls a width that cannot hold its value "a lie".
    for m in re.finditer(r"SourceDesc\.const\s+(\d+)\s+\(\(?(-?)Int\.ofNat\s+(\d+)\)?\)", text):
        w, neg, v = int(m.group(1)), m.group(2), int(m.group(3))
        if not neg and v >= (1 << w):
            fails.append(f"const width {w} cannot hold value {v} (needs {v.bit_length()} bits)")

    # ROM entries must fit the table's data width.
    for m in re.finditer(r"SourceDesc\.memConst\s+(\d+)\s+(\d+)\s+#\[([^\]]*)\]", text):
        dw = int(m.group(2))
        body = m.group(3).strip()
        for i, e in enumerate(body.split(",") if body else []):
            ev = int(e)
            if ev < 0 or ev >= (1 << dw):
                fails.append(f"ROM entry {i} = {ev} does not fit dw={dw}")

    # --- no placeholders ---------------------------------------------------
    n_sorry = len(re.findall(r"\bsorry\b", text))
    say(f"sorry={n_sorry}")
    if n_sorry:
        fails.append(f"{n_sorry} `sorry`")

    for f in fails:
        print(f"FAIL: {f}")
    if not fails:
        say("PASS: all verified_compiler static gates")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
