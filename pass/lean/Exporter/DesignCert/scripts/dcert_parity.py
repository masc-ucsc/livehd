#!/usr/bin/env python3
"""Cross-language DCERT2 parity: C++ bytes against the verified Lean codec.

WHAT IS COMPARED, AND WHAT THAT PROVES.

`pass.lean` walks a `CertificateIR` and publishes two descriptions of it: the
Lean SOURCE literal (`<base>_designCert`) and the DCERT2 byte stream
(`<base>.dcert`).  This harness elaborates the literal, renders it with the
library's own `renderCert`, and compares the result to the C++ bytes.

Byte equality is not a weaker check than a field-by-field walk -- it is a
STRONGER one, and that is a theorem rather than a hope:
`Compiler.CertIO.renderCert_injective` says `renderCert A = renderCert B -> A =
B`, proved from the unconditional round trip.  So equal bytes mean the C++
emitter and the Lean literal describe the same certificate in every field,
including ones no hand-written comparison would have thought to look at.

It then runs the VERIFIED reader over the production bytes and checks
`renderCert (parseCert bytes) = bytes`, and exercises the refusal controls:
version, truncation, trailing data, a malformed clock separator, a short name,
an unsupported opcode -- plus a SENSITIVITY control, because a comparison that
cannot fail proves nothing, and the codec/admission split on real data.

WHAT IT DOES NOT PROVE.  That the `CertificateIR` is a faithful transcription
of the LGraph.  That link is `pass.lean`'s, unchanged by any of this.
"""
import argparse, hashlib, os, pathlib, re, subprocess, sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent.parent.parent.parent


def cert_block(text: str, base: str) -> str:
    """The `<base>_designCert` definition, verbatim.

    Taken by its exact opening line and closed on the emitter's own `  }` --
    not by a brace count, which an `Int` literal or a string containing a brace
    would break. A missing or unterminated block is a REFUSAL: guessing at the
    extent of a certificate is how a parity run ends up comparing a design to
    part of itself.
    """
    head = f"def {base}_designCert : DesignCert :="
    lines = text.splitlines()
    try:
        start = next(i for i, l in enumerate(lines) if l.strip() == head)
    except StopIteration:
        raise SystemExit(f"no `{head}` in the emitted module")
    for j in range(start + 1, len(lines)):
        if lines[j] == "  }":
            return "\n".join(lines[start:j + 1])
    raise SystemExit(f"`{base}_designCert` is never closed by a `  }}` line")


PROBE = r'''
import LeanSemanticPrimitives.Compiler.CertIOParity

set_option maxRecDepth 1000000
set_option maxHeartbeats 0

open Compiler
open Compiler.CertIO

%(cert)s

def certPath : System.FilePath := "%(path)s"

/-- Byte offset of the first difference, or of the truncation point. -/
def firstDiff (a b : ByteArray) : Option Nat :=
  match (List.range (min a.size b.size)).find? (fun i => a[i]! != b[i]!) with
  | some i => some i
  | none   => if a.size == b.size then none else some (min a.size b.size)

def show8 (b : ByteArray) (at_ : Nat) : String :=
  String.intercalate " " ((List.range 8).filterMap fun k =>
    if at_ + k < b.size then some (toString (b[at_ + k]!).toNat) else none)

abbrev Log := IO.Ref (Array String)

def ok (_lg : Log) (m : String) : IO Unit := do IO.println s!"  ok   {m}"
def bad (lg : Log) (m : String) : IO Unit := do
  IO.println s!"  FAIL {m}"
  lg.modify (fun a => a.push m)

def expectError (lg : Log) (what : String) (r : Except String DesignCert) : IO Unit :=
  match r with
  | .error e => ok lg s!"{what} refused: {e}"
  | .ok _    => bad lg s!"{what} was ACCEPTED"

def expectOk (lg : Log) (what : String) (r : Except String DesignCert) : IO Unit :=
  match r with
  | .ok _    => ok lg s!"{what} accepted"
  | .error e => bad lg s!"{what} refused: {e}"

def main : IO UInt32 := do
  let lg : Log ← IO.mkRef #[]
  let raw ← IO.FS.readBinFile certPath
  let mine := renderCert %(base)s_designCert
  IO.println s!"DCERT2 parity: %(base)s  ({raw.size} bytes from C++, {mine.size} from renderCert)"

  -- 1. BYTE PARITY. By `renderCert_injective` this is field equality.
  match firstDiff raw mine with
  | none   => ok lg "byte parity with renderCert of the emitted literal"
  | some i => bad lg s!"bytes differ at offset {i}: C++ [{show8 raw i}] vs Lean [{show8 mine i}]"

  -- 2. THE VERIFIED READER, on the production bytes.
  match parseCert raw with
  | .error e => bad lg s!"the verified reader refused production bytes: {e}"
  | .ok D => do
    ok lg s!"parsed: {D.sources.size} sources, {D.nodes.size} nodes, {D.outputs.size} outputs, {D.flops.size} flops, {D.memories.size} memories, {D.clocks.size} clocks"
    if renderCert D == raw then ok lg "re-rendering the parsed design reproduces the bytes"
      else bad lg "re-rendering the parsed design does NOT reproduce the bytes"
    expectOk lg "semantic admission of the production certificate" (readCertChecked raw)

    -- 3. SENSITIVITY. A comparison that cannot fail proves nothing.
    if D.flops.isEmpty then
      IO.println "  --   no flops: flop sensitivity controls skipped"
    else
      let widened := { D with flops := D.flops.modify 0 (fun f => { f with width := f.width + 1 }) }
      if renderCert widened == raw then bad lg "changing a flop width left the bytes identical"
        else ok lg "sensitive to a flop width"
      let reclocked := { D with flops := D.flops.modify 0 (fun f => { f with clock := f.clock + 1 }) }
      if renderCert reclocked == raw then bad lg "changing a flop clock ordinal left the bytes identical"
        else ok lg "sensitive to a flop clock ordinal"
      let async := { D with flops := D.flops.modify 0 (fun f => { f with asyncReset := !f.asyncReset }) }
      if renderCert async == raw then bad lg "flipping asyncReset left the bytes identical"
        else ok lg "sensitive to asyncReset"
      -- 4. CODEC vs ADMISSION, on real data: an out-of-range ordinal is a
      -- perfectly good record and a refused DESIGN.
      let wild := renderCert { D with flops := D.flops.modify 0 (fun f => { f with clock := 999 }) }
      expectOk    lg "codec reads a flop on clock ordinal 999 (it judges nothing)" (parseCert wild)
      expectError lg "admission refuses that same certificate" (readCertChecked wild)
    let extra := { D with clocks := D.clocks.push { name := "extra" } }
    if renderCert extra == raw then bad lg "adding a clock left the bytes identical"
      else ok lg "sensitive to the clock table"

  -- 5. REFUSAL CONTROLS on the production bytes.
  expectError lg "DCERT1 magic" (parseCert (raw.set! 5 49))
  expectError lg "truncation (last 10 bytes removed)"
    (parseCert (raw.extract 0 (raw.size - 10)))
  expectError lg "trailing non-whitespace" (parseCert (raw.push 120))
  expectOk    lg "trailing whitespace (allowed by the format)" (parseCert (raw.push 10))
  expectError lg "unsupported opcode 99"
    (parseCert "DCERT2\n0\n1\n99 0 8 0 7\n0\n0\n0\n1\n5 clock\n".toUTF8)
  expectError lg "clock name separator is not a space"
    (parseCert "DCERT2\n0\n0\n0\n0\n0\n1\n5xclock\n".toUTF8)
  expectError lg "clock name shorter than its count"
    (parseCert "DCERT2\n0\n0\n0\n0\n0\n1\n9 clk\n".toUTF8)
  expectOk    lg "a clock name containing a space and a newline"
    (parseCert "DCERT2\n0\n0\n0\n0\n0\n1\n7 a b\nc d\n".toUTF8)

  let fails ← lg.get
  if fails.isEmpty then
    IO.println "DCERT2-PARITY OK"
    return 0
  else
    IO.println s!"DCERT2-PARITY FAILED ({fails.size})"
    return 1
'''


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", required=True)
    ap.add_argument("--lean", required=True, help="the emitted <base>_Lgraph.lean")
    ap.add_argument("--dcert", required=True, help="the emitted <base>.dcert")
    ap.add_argument("--iso", default=str(ROOT / "generated/port_staging/iso"),
                    help="build root holding CertIOParity.olean")
    ap.add_argument("--out", required=True, help="directory for the probe and its log")
    ap.add_argument("--toolchain",
                    default=os.path.join(os.environ.get("ELAN_HOME", os.path.expanduser("~/.elan")), "toolchains/leanprover--lean4---v4.31.0"))
    a = ap.parse_args()

    lean_src = pathlib.Path(a.lean).read_text(encoding="utf-8")
    dcert = pathlib.Path(a.dcert).resolve()
    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    probe = out / f"parity_{a.base}.lean"
    probe.write_text(PROBE % {"cert": cert_block(lean_src, a.base),
                              "path": str(dcert), "base": a.base}, encoding="utf-8")

    iso = pathlib.Path(a.iso).resolve()
    lean_dir = ROOT / "formal/lean"
    pkgs = sorted(lean_dir.glob(".lake/packages/*/.lake/build/lib/lean"))
    lean_path = os.pathsep.join([str(iso / "build/lib/lean")] + [str(p) for p in pkgs]
                                + [str(lean_dir / ".lake/build/lib/lean")])
    env = dict(os.environ, LEAN_PATH=lean_path, LEAN_NUM_THREADS="2")

    log = out / f"parity_{a.base}.log"
    # `--run`, not `lake env`: the libraries are already built and the probe is a
    # single file. No `--` separator -- that is a D2 docstring error that cost a
    # run. Resource limits stay the ordinary ones; nothing here raises a stack.
    cmd = ["nice", "-n19", "ionice", "-c3",
           "/usr/bin/time", "-v", "-o", str(out / f"parity_{a.base}.time"),
           f"{a.toolchain}/bin/lean", "--run", str(probe)]
    r = subprocess.run(cmd, env=env, capture_output=True, text=True)
    log.write_text(r.stdout + r.stderr, encoding="utf-8")
    sys.stdout.write(r.stdout)
    if r.returncode != 0:
        sys.stderr.write(r.stderr)
    digest = hashlib.sha256(dcert.read_bytes()).hexdigest()
    print(f"{a.base}: exit={r.returncode} dcert_sha256={digest[:16]} log={log}")
    return r.returncode


if __name__ == "__main__":
    raise SystemExit(main())
