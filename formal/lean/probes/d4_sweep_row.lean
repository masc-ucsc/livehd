/-
One sweep row, from a DCERT2 file.  RUNTIME path only: this never elaborates or
typechecks a generated `_Lgraph.lean`, which is the whole point of Direction 4 --
`DCERT2 -> CertIO.loadCert -> runCheckedAllEdges`.

Prints one tab-separated row so the bash harness never has to parse prose:

  sources  nodes  flops  memories  parse_status  parse_ms  compiles_ok
  runtime_status  run_ms  digest  diagnostic

Every field is non-empty -- `-` where there is nothing to say -- because command
substitution strips a trailing empty field and would silently shift the two
columns the harness appends.

An absent or unreadable file is a ROW with `parse_status=READ_ERROR`, never a
missing row: the harness must be unable to turn an absent result into a success.

The four failure kinds the sweep has to tell apart are distinguishable here:
  READ_ERROR    the certificate is not there or not readable
  PARSE_ERROR   the certificate is malformed  (`parseCert`)
  REFUSED       `compilesOk` is false -- the VERIFIED COMPILER declines the
                design; the certificate itself is fine
  RUN_ERROR     accepted, but evaluation failed
Front-end/emission failure is upstream of this file and is the harness's job.

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
-/
import LeanSemanticPrimitives.Compiler.CertIO
open Compiler CertIO

def main (args : List String) : IO Unit := do
  let path := args.headD ""
  if path == "" then
    IO.println "-\t-\t-\t-\tREAD_ERROR\t0\t-\tNOT_RUN\t0\t-\tno certificate path given"
    return
  let bytes? ← (do
      let b ← IO.FS.readBinFile path
      pure (some b)) <|> pure none
  match bytes? with
  | none =>
      IO.println s!"-\t-\t-\t-\tREAD_ERROR\t0\t-\tNOT_RUN\t0\t-\tcannot read {path}"
  | some b =>
    if b.size == 0 then
      IO.println s!"-\t-\t-\t-\tREAD_ERROR\t0\t-\tNOT_RUN\t0\t-\tempty certificate {path}"
    else
      let t0 ← IO.monoMsNow
      match parseCert b with
      | .error e =>
          let t1 ← IO.monoMsNow
          IO.println s!"-\t-\t-\t-\tPARSE_ERROR\t{t1 - t0}\t-\tNOT_RUN\t0\t-\t{e}"
      | .ok D =>
        let t1 ← IO.monoMsNow
        let shape := s!"{D.sources.size}\t{D.nodes.size}\t{D.flops.size}\t{D.memories.size}"
        let ok := compilesOk D
        if !ok then
          -- The certificate parsed; the compiler declines the DESIGN.  Reported
          -- as its own kind so it is never read as a parse or runtime failure.
          IO.println s!"{shape}\tPARSED\t{t1 - t0}\tfalse\tREFUSED\t0\t-\tcompilesOk is false"
        else
          let (inp, st) := stimulus D 42
          let t2 ← IO.monoMsNow
          match runCheckedAllEdges D inp st with
          | .error e =>
              let t3 ← IO.monoMsNow
              IO.println s!"{shape}\tPARSED\t{t1 - t0}\ttrue\tRUN_ERROR\t{t3 - t2}\t-\t{e}"
          | .ok r =>
              let t3 ← IO.monoMsNow
              IO.println s!"{shape}\tPARSED\t{t1 - t0}\ttrue\tRAN\t{t3 - t2}\t{hash (digest r)}\t-"
