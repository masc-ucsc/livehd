/-
# `lhd_ckpt` — check, compare and make `lhdcert-ckpt v1` checkpoints

```
lhd_ckpt check     <cert> <ckpt>                 parse (digest) and validate against the certificate
lhd_ckpt diff      <a.ckpt> <b.ckpt>             first difference, or SAME
lhd_ckpt seal      <in.ckpt> <out.ckpt>          recompute the digest of a hand-edited checkpoint
lhd_ckpt normalize <in.ckpt> <out.ckpt>          sort words, drop words equal to the default, reseal
lhd_ckpt init      <cert> <out.ckpt> [reset|zero]  the power-on state (default reset) at cycle 0
lhd_ckpt to-state  <cert> <ckpt> <out>           the same state as an lgraph-sim `--state` file
```

`<cert>` is a DCERT4, DCERT3 or DCERT1 file. Exit codes: 0 ok / SAME, 1 refused / DIFFER,
2 usage or I/O, 3 the checkpoint does not parse.

The format and every refusal are in `Compiler/Ckpt.lean`; `pass/lean/tools/formats/ckpt.md`
documents them.
-/
import LeanSemanticPrimitives.Compiler.Ckpt
import LeanSemanticPrimitives.Compiler.CertIORoute
import LeanSemanticPrimitives.Compiler.DirectTrace

open Compiler Compiler.Ckpt

def loadCert (path : String) : IO DesignCert := do
  let b ← IO.FS.readBinFile path
  match CertIO.admitAny b with
  | .ok (_, D) => pure D
  | .error e   => throw (IO.userError s!"{path}: {e}")

def readCkpt (path : String) (checkDigest : Bool := true) : IO (Except String Ckpt) := do
  return parse (← IO.FS.readBinFile path) checkDigest

def usage : String :=
  "usage: lhd_ckpt check <cert> <ckpt> | diff <a> <b> | seal <in> <out> | normalize <in> <out>\n" ++
  "       | init <cert> <out> [reset|zero] | to-state <cert> <ckpt> <out>"

def main (args : List String) : IO UInt32 := do
  try
    match args with
    | ["check", cert, ck] =>
      let D ← loadCert cert
      match ← readCkpt ck with
      | .error e => IO.eprintln e; return 3
      | .ok c =>
        match validate D (designHash D) c with
        | some e => IO.println s!"REFUSED: {e}"; return 1
        | none =>
          IO.println (s!"OK: cycle {c.cycle}, {c.flops.size} flops, {c.mems.size} memories, " ++
                      s!"{c.exts.size} ext sections")
          return 0
    | ["diff", a, b] =>
      let ra ← readCkpt a
      let rb ← readCkpt b
      match ra, rb with
      | .error e, _ => IO.eprintln e; return 3
      | _, .error e => IO.eprintln e; return 3
      | .ok ca, .ok cb =>
        match Ckpt.diff ca cb with
        | none   => IO.println "SAME"; return 0
        | some d => IO.println s!"DIFFER: {d}"; return 1
    | ["seal", i, o] =>
      match ← readCkpt i (checkDigest := false) with
      | .error e => IO.eprintln e; return 3
      | .ok c => IO.FS.writeBinFile o (render c); return 0
    | ["normalize", i, o] =>
      match ← readCkpt i (checkDigest := false) with
      | .error e => IO.eprintln e; return 3
      | .ok c => IO.FS.writeBinFile o (render (normalize c)); return 0
    | "init" :: cert :: o :: rest =>
      let D ← loadCert cert
      let s ← match rest with
        | [] | ["reset"] => pure (Direct.resetState D)
        | ["zero"]       => pure (Direct.zeroState D)
        | _ => throw (IO.userError usage)
      IO.FS.writeBinFile o (render (save D (designHash D) 0 #[] s))
      return 0
    | ["to-state", cert, ck, o] =>
      let D ← loadCert cert
      match ← readCkpt ck with
      | .error e => IO.eprintln e; return 3
      | .ok c =>
        match validate D (designHash D) c with
        | some e => IO.println s!"REFUSED: {e}"; return 1
        | none => IO.FS.writeFile o (toStateText c); return 0
    | _ => IO.eprintln usage; return 2
  catch e =>
    IO.eprintln s!"lhd_ckpt: {e}"
    return 2
