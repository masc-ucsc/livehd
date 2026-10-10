#!/usr/bin/env python3
"""Generate the per-group chunk-fact modules for one design.

A chunk fact is checked by the kernel when its module is BUILT, and auditing it
afterwards through an import costs ~7 MB -- measured -- because `collectAxioms`
walks dependency names rather than re-checking imported bodies. Proving all 38
of a 1,186-binding design in one process instead dies in `auditOrThrow` at
>14 GB. So the facts are split across modules and the composition imports them.

Layout, under LeanSemanticPrimitives/Gen/<Design>/:

    Cert.lean   the certificate, elaborated ONCE; every group imports it
    G0.lean     chunk facts for the first group
    G1.lean     imports G0, so prefixes can chain in module order
    ...

PROVENANCE. Each module records the certificate's sha256 and the generator's
tool digest, and `Cert.lean` records them too. Modules built from different
certificate or generator revisions therefore carry different constants, and a
composition can refuse to mix them rather than silently proving something about
a design that is no longer the one on disk.
"""
import argparse, hashlib, importlib.util, pathlib, re, sys

HERE = pathlib.Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location("d3_sweep_mod", HERE / "d3_sweep.py")
sweep = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(sweep)

GEN_ROOT = HERE.parent.parent.parent / "formal/lean/LeanSemanticPrimitives/Gen"


def cert_body(cert: pathlib.Path) -> str:
    out = []
    for line in cert.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("theorem "):
            break
        if line.startswith("import "):
            continue
        out.append(line)
    txt = "\n".join(out)
    # The sweep trims the certificate the same way and then APPENDS its probe
    # tail, so a doc comment introducing the first theorem lands on a real
    # declaration. A module ends here, so an unterminated one is a parse error
    # at end of input -- drop it.
    # A doc comment must attach to a declaration. Whether it is terminated or
    # not, a trailing one is a parse error at end of input, so drop it.
    txt = txt.rstrip()
    cut = txt.rfind("/--")
    if cut != -1 and not re.search(r"\b(def|theorem|abbrev|instance)\b", txt[cut:]):
        txt = txt[:cut]
    return txt.rstrip()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--cert", required=True)
    ap.add_argument("--module", required=True, help="design/module name")
    ap.add_argument("--chunk-size", type=int, default=32)
    ap.add_argument("--per-group", type=int, default=4)
    ap.add_argument("--groups", type=int, default=0, help="0 = all")
    ap.add_argument("--bindings", type=int, required=True)
    ap.add_argument("--gen-digest", default="",
                    help="digest to embed as <module>_genSha. The runner passes "
                         "its own runner_digest so the embedded value and the "
                         "run's provenance.json agree; defaulting to "
                         "sweep.tool_digest() would make them differ silently.")
    a = ap.parse_args()

    cert = pathlib.Path(a.cert)
    csha = hashlib.sha256(cert.read_bytes()).hexdigest()
    tsha = a.gen_digest or sweep.tool_digest()
    ns = re.sub(r"[^A-Za-z0-9]", "", a.module.title())
    out = GEN_ROOT / ns
    out.mkdir(parents=True, exist_ok=True)

    (out / "Cert.lean").write_text(
        "import LeanSemanticPrimitives.Compiler.ReifyProof\n"
        f"-- GENERATED from {cert.name}; do not edit.\n"
        f"def {a.module}_certSha : String := \"{csha}\"\n"
        f"def {a.module}_genSha  : String := \"{tsha}\"\n"
        + cert_body(cert) + "\n", encoding="utf-8")

    # A design with no bindings still gets ONE chunk, of length zero.
    # `prove_reified_chunked` already treats `nb == 0` as one chunk, so a
    # generator that produced none left the composition importing `G-1`.
    # Measured: both zero-node designs in the first cohort failed exactly
    # there, with `object file ... Gen/<Ns>/G.olean does not exist`.
    nchunk = max(1, (a.bindings + a.chunk_size - 1) // a.chunk_size)
    ngroup = (nchunk + a.per_group - 1) // a.per_group
    if a.groups:
        ngroup = min(ngroup, a.groups)
    for g in range(ngroup):
        prev = (f"import LeanSemanticPrimitives.Gen.{ns}.G{g-1}\n" if g
                else f"import LeanSemanticPrimitives.Gen.{ns}.Cert\n")
        lines = [prev, f"-- GENERATED; cert {csha[:16]} gen {tsha[:16]}\n",
                 # Options do not cross module boundaries: the certificate's
                 # own `set_option`s apply inside Cert.lean only, and a chunk
                 # fact over 32 nodes exceeds the default recursion depth.
                 "set_option maxRecDepth 1000000\n",
                 "set_option maxHeartbeats 0\n",
                 # Namespaced per design: `cf0` is otherwise a global name, so
                 # importing two designs into one session would collide.
                 f"namespace {ns}\n"]
        for j in range(g * a.per_group, min((g + 1) * a.per_group, nchunk)):
            lo = j * a.chunk_size
            ln = min(a.chunk_size, a.bindings - lo)
            lines.append(f"compile_chunk_fact {a.module}_designCert "
                         f"from {lo} take {ln} as cf{j}\n")
        lines.append(f"end {ns}\n")
        (out / f"G{g}.lean").write_text("".join(lines), encoding="utf-8")
    print(f"generated {out.relative_to(GEN_ROOT.parent.parent.parent)}: "
          f"Cert + G0..G{ngroup-1}  ({nchunk} chunks of {a.chunk_size}, "
          f"{a.per_group} per group)")
    print(f"  cert sha {csha[:16]}  tool sha {tsha[:16]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
