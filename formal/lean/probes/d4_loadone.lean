import CertIO
open Compiler CertIO in
def main (args : List String) : IO Unit := do
  let name := args.headD "btb_gate"
  let p := s!"CERTDIR/{name}.pyconv"
  let t0 ← IO.monoMsNow
  let b ← IO.FS.readBinFile p
  let t1 ← IO.monoMsNow
  match parseCert b with
  | .error e => IO.println s!"{name}\tPARSE_ERROR\t{e}"
  | .ok D =>
    let t2 ← IO.monoMsNow
    let ok := compilesOk D
    let t3 ← IO.monoMsNow
    let (inp, st) := stimulus D 42
    match runChecked D inp st with
    | .error e => IO.println s!"{name}\tREFUSED\t{e}"
    | .ok r =>
      let t4 ← IO.monoMsNow
      IO.println s!"RESULT\t{name}\t{D.nodes.size}\tread={t1-t0}\tparse={t2-t1}\tcompilesOk={t3-t2}({ok})\trun={t4-t3}\tdigest={hash (digest r)}"
