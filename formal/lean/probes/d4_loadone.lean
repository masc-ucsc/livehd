/-
One certificate, loaded and run, with the phase timings split out.

RESTORED from pinned D4 f6ad1fe29f8f15aeda045115864fd81a34c33aef. Two
adaptations, and nothing else:

  * the format is DCERT2. It carries the clock table, `FlopDesc.clock`,
    `asyncReset` and `MemoryDesc.clock`, and `parseCert_renderCert` proves the
    codec total and invertible for EVERY `DesignCert`. A historical DCERT1 file
    is refused by magic rather than reinterpreted as a one-clock design.
  * the runtime entry point is `runCheckedAllEdges`, the NAMED all-edges
    contract. This procedure was written before clock edges existed and means
    exactly that; naming it keeps the assumption visible at the call site
    instead of defaulting it underneath.
  * the certificate PATH is an argument. The pinned version embedded the
    literal `CERTDIR/<name>.pyconv`, a placeholder a caller had to `sed` before
    every use -- which is a template, not a runnable procedure, and a stale
    substitution is invisible in the output. The directory now comes from
    `D4_CERT_DIR` or the second argument, a name carrying no extension gets
    `.dcert` (what the pass publishes), and the path actually used is PRINTED
    on every row so no run is ambiguous about which bytes it read.

Usage:  lake env lean --run probes/d4_loadone.lean <name-or-path> [dir]
-/
import LeanSemanticPrimitives.Compiler.CertIO
open Compiler CertIO in
def main (args : List String) : IO Unit := do
  let name := args.headD "btb_gate"
  let dirArg := (args.drop 1).head?
  let dirEnv ← IO.getEnv "D4_CERT_DIR"
  -- An explicit argument wins over the environment; neither is replaced by a
  -- fallback once set. With neither, a bare name is read from the cwd.
  let dir := dirArg.getD (dirEnv.getD "")
  let leaf := if name.endsWith ".dcert" || name.contains '.' then name else name ++ ".dcert"
  let p : System.FilePath := if dir == "" then leaf else dir ++ "/" ++ leaf
  let t0 ← IO.monoMsNow
  let b ← IO.FS.readBinFile p
  let t1 ← IO.monoMsNow
  match parseCert b with
  | .error e => IO.println s!"{name}\t{p}\tPARSE_ERROR\t{e}"
  | .ok D =>
    let t2 ← IO.monoMsNow
    let ok := compilesOk D
    let t3 ← IO.monoMsNow
    let (inp, st) := stimulus D 42
    match runCheckedAllEdges D inp st with
    | .error e => IO.println s!"{name}\t{p}\tREFUSED\t{e}"
    | .ok r =>
      let t4 ← IO.monoMsNow
      IO.println s!"RESULT\t{name}\t{p}\t{D.nodes.size}\tread={t1-t0}\tparse={t2-t1}\tcompilesOk={t3-t2}({ok})\trun={t4-t3}\tdigest={hash (digest r)}"
