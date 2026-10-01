#!/usr/bin/env python3
"""Negative tests for `d3_manifest.py`.

A manifest's job is to stop a denominator from drifting, so the cases that
matter are the ones where it would drift silently: a duplicated target, a
denominator of the wrong size, a frozen target that has since lost its
certificate, a certificate whose bytes changed under a stable name.

Each case builds a throwaway corpus under the project's own temp directory and
asserts the manifest either REFUSES or records the change as current evidence —
never that it quietly carries stale metadata forward.

Run: python3 pass/lean/scripts/d3_manifest_test.py
"""

import csv
import importlib.util
import pathlib
import shutil
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
spec = importlib.util.spec_from_file_location("d3_manifest", HERE / "d3_manifest.py")
dm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dm)

CERT = """-- generated fixture
import LeanSemanticPrimitives.Compiler.CompileDesign
def {name}_designCert : DesignCert := {{ sources := #[], nodes := #[], outputs := #[], flops := #[], memories := #[] }}
"""


def make_corpus(tmp: pathlib.Path, blocks, suffix="_gate", body_salt=""):
    d = tmp / "certs" / "lean"
    d.mkdir(parents=True, exist_ok=True)
    for b in blocks:
        (d / f"{b}{suffix}_Lgraph.lean").write_text(CERT.format(name=b + suffix) + body_salt)
    with (d.parent / "emit.tsv").open("w") as fh:
        fh.write("module\temit\tsources\tnodes\tflops\tmems\n")
        for i, b in enumerate(blocks):
            fh.write(f"{b}\tEMITTED\t{10 + i}\t{100 + i}\t0\t0\n")
    return d


def make_plan(tmp: pathlib.Path, blocks) -> pathlib.Path:
    p = tmp / "plan.md"
    body = "### TOP LEVEL  (1)\n\n" + "".join(f"- `{b}` [x.sv]\n" for b in blocks) + "\n## next\n"
    p.write_text("# plan\n\n" + body)
    return p


def make_sweep(tmp: pathlib.Path, modules) -> pathlib.Path:
    p = tmp / "sweep.tsv"
    with p.open("w") as fh:
        fh.write("module\tverdict\tbucket\tsources\tnodes\tflops\tmemories\n")
        for m in modules:
            fh.write(f"{m}\tPROVEN\tproven\t10\t20\t0\t0\n")
    return p


def header_digest(path: pathlib.Path) -> str:
    """The `target30_sha256=` the manifest stamped into cva6_30.tsv's header.

    Read back from the FILE rather than trusted from the return value: the
    header is what a later reader joins against, so a return value that did not
    reach the file would be a silent inconsistency.
    """
    for line in path.read_text().splitlines():
        if line.startswith("#") and "target30_sha256=" in line:
            return line.split("target30_sha256=", 1)[1].strip()
    return ""


def expect_error(name, fn, note) -> bool:
    try:
        fn()
    except dm.ManifestError as e:
        print(f"ok   {name:<26} refused: {str(e)[:70]}")
        return True
    except Exception as e:  # noqa: BLE001
        print(f"FAIL {name:<26} wrong exception {type(e).__name__}: {e}")
        return False
    print(f"FAIL {name:<26} accepted silently — {note}")
    return False


def main() -> int:
    failures = []
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="d3_manifest_test_", dir=str(ROOT / "temp")))
    try:
        blocks = [f"b{i:02d}" for i in range(dm.EXPECT_CVA6_BLOCKS)]
        mods = [f"m{i:03d}" for i in range(dm.EXPECT_COREET)]

        # --- CORE-ET: duplicate target in the denominator --------------------
        dup = tmp / "dup"
        dup.mkdir()
        sweep_dup = make_sweep(dup, mods[:-1] + [mods[0]])
        certs_dup = make_corpus(dup, [])
        if not expect_error("coreet_duplicate",
                            lambda: dm.coreet_manifest(certs_dup, dup, sweep_dup),
                            "a duplicated module inflates the denominator"):
            failures.append("coreet_duplicate")

        # --- CORE-ET: wrong denominator size ---------------------------------
        wrong = tmp / "wrong"
        wrong.mkdir()
        sweep_wrong = make_sweep(wrong, mods[:-1])
        certs_wrong = make_corpus(wrong, [])
        if not expect_error("coreet_wrong_count",
                            lambda: dm.coreet_manifest(certs_wrong, wrong, sweep_wrong),
                            f"{dm.EXPECT_COREET - 1} targets is not {dm.EXPECT_COREET}"):
            failures.append("coreet_wrong_count")

        # --- CORE-ET: certificate not named in the denominator ---------------
        orph = tmp / "orph"
        orph.mkdir()
        sweep_o = make_sweep(orph, mods)
        certs_o = make_corpus(orph, ["not_a_target"], suffix="")
        if not expect_error("coreet_orphan_cert",
                            lambda: dm.coreet_manifest(certs_o, orph, sweep_o),
                            "an orphan certificate means the join is unsound"):
            failures.append("coreet_orphan_cert")

        # --- CVA6: wrong authoritative count ---------------------------------
        wb = tmp / "wb"
        wb.mkdir()
        plan_wb = make_plan(wb, blocks[:-1])
        certs_wb = make_corpus(wb, blocks[:5])
        if not expect_error("cva6_wrong_block_count",
                            lambda: dm.cva6_manifests(certs_wb, wb, False, plan_wb),
                            "the authoritative list must be exactly 78"):
            failures.append("cva6_wrong_block_count")

        # --- CVA6: `_gate` strip collision ------------------------------------
        col = tmp / "col"
        col.mkdir()
        plan_col = make_plan(col, blocks)
        certs_col = make_corpus(col, blocks[:5])
        (certs_col / f"{blocks[0]}_Lgraph.lean").write_text(CERT.format(name=blocks[0]))
        if not expect_error("cva6_gate_collision",
                            lambda: dm.cva6_manifests(certs_col, col, False, plan_col),
                            "`b00` and `b00_gate` both map to block `b00`"):
            failures.append("cva6_gate_collision")

        # --- CVA6: a frozen target that lost its certificate ------------------
        fz = tmp / "fz"
        fz.mkdir()
        plan_fz = make_plan(fz, blocks)
        certs_fz = make_corpus(fz, blocks[:35])
        dm.cva6_manifests(certs_fz, fz, False, plan_fz)          # creates the freeze
        frozen = [r["block"] for r in csv.DictReader(
            (l for l in (fz / "cva6_30_frozen.tsv").open() if not l.startswith("#")),
            delimiter="\t")]
        victim = frozen[0]
        (certs_fz / f"{victim}_gate_Lgraph.lean").unlink()
        res = dm.cva6_manifests(certs_fz, fz, False, plan_fz)     # re-join
        rows = list(csv.DictReader(
            (l for l in (fz / "cva6_30.tsv").open() if not l.startswith("#")), delimiter="\t"))
        if len(rows) != dm.EXPECT_CVA6_SELECTED:
            print(f"FAIL frozen_missing_cert      cva6_30.tsv has {len(rows)} rows, "
                  f"expected {dm.EXPECT_CVA6_SELECTED}")
            failures.append("frozen_missing_cert_rows")
        row = next((r for r in rows if r["block"] == victim), None)
        if row is None or row["cert_available"] != "0" or not row["blocked_cause"]:
            print(f"FAIL frozen_missing_cert      lost target is not an explicit row: {row}")
            failures.append("frozen_missing_cert")
        elif row["sha256"] or row["nodes"]:
            print(f"FAIL frozen_missing_cert      stale metadata carried forward: {row}")
            failures.append("frozen_missing_cert_stale")
        else:
            print(f"ok   frozen_missing_cert      {victim} kept as an explicit "
                  f"cert_available=0 row, no stale metadata")

        # --- CVA6: duplicate module rows in emit.tsv --------------------------
        de = tmp / "de"
        de.mkdir()
        plan_de = make_plan(de, blocks)
        certs_de = make_corpus(de, blocks[:5])
        et = certs_de.parent / "emit.tsv"
        et.write_text(et.read_text() + f"{blocks[0]}\tEMITTED\t10\t100\t0\t0\n")
        if not expect_error("cva6_duplicate_emit",
                            lambda: dm.cva6_manifests(certs_de, de, False, plan_de),
                            "a duplicated emit.tsv row makes the size metadata ambiguous"):
            failures.append("cva6_duplicate_emit")

        # --- CVA6: a certificate whose bytes changed --------------------------
        ch = tmp / "ch"
        ch.mkdir()
        plan_ch = make_plan(ch, blocks)
        certs_ch = make_corpus(ch, blocks[:35])
        res_before = dm.cva6_manifests(certs_ch, ch, False, plan_ch)
        before_digest = res_before["target30_digest"]
        before_header = header_digest(ch / "cva6_30.tsv")
        before = list(csv.DictReader(
            (l for l in (ch / "cva6_30.tsv").open() if not l.startswith("#")), delimiter="\t"))
        if before_header != before_digest:
            print(f"FAIL cert_changed             header digest {before_header[:12]} != "
                  f"returned {before_digest[:12]} before the change")
            failures.append("cert_changed_header_before")

        tgt = before[0]["block"]
        f = certs_ch / f"{tgt}_gate_Lgraph.lean"
        f.write_text(f.read_text() + "\n-- regenerated\n")

        res2 = dm.cva6_manifests(certs_ch, ch, False, plan_ch)
        after = list(csv.DictReader(
            (l for l in (ch / "cva6_30.tsv").open() if not l.startswith("#")), delimiter="\t"))
        b0 = next(r for r in before if r["block"] == tgt)
        a0 = next(r for r in after if r["block"] == tgt)
        frozen_txt = (ch / "cva6_30_frozen.tsv").read_text()

        if a0["sha256"] == b0["sha256"]:
            print("FAIL cert_changed             cva6_30.tsv still shows the old hash")
            failures.append("cert_changed")
        elif b0["sha256"] not in frozen_txt:
            print("FAIL cert_changed             the freeze file should retain the "
                  "at-freeze hash as history")
            failures.append("cert_changed_history")
        else:
            print(f"ok   cert_changed             current hash updated "
                  f"({b0['sha256'][:8]} -> {a0['sha256'][:8]}), freeze keeps history")

        # The digest must MOVE with the content -- same fixture, before vs after.
        if res2["target30_digest"] == before_digest:
            print(f"FAIL target30_digest_moves    digest unchanged "
                  f"({before_digest[:12]}) after a certificate was rewritten")
            failures.append("target30_digest_moves")
        else:
            print(f"ok   target30_digest_moves    {before_digest[:12]} -> "
                  f"{res2['target30_digest'][:12]} after the rewrite")

        # ...and the value written into the file must match the value returned.
        after_header = header_digest(ch / "cva6_30.tsv")
        if after_header != res2["target30_digest"]:
            print(f"FAIL target30_digest_header   header {after_header[:12]} != "
                  f"returned {res2['target30_digest'][:12]}")
            failures.append("target30_digest_header")
        else:
            print(f"ok   target30_digest_header   cva6_30.tsv header matches the "
                  f"returned digest ({after_header[:12]})")

    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    print("\nMANIFEST-TEST", "OK" if not failures else f"FAILED: {failures}")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
