/-
The ordinary-write admission route, run on ACTUALLY RETAINED certificates.

§63.58's census found exactly one operator gap across the whole retained
corpus -- `Op_MemWrite` -- and named the five certificates it refuses.  This
driver loads each one and prints BOTH answers:

  * `admitsB`  — the ORIGINAL checker, whose contract is unchanged and which
                 still refuses a plain write;
  * `admitsWB` — the EXTENDED checker.

It also counts the plain-write nodes, so a design that is refused for some
OTHER reason is not silently read as evidence about this one.  A certificate
with no plain write must give the SAME answer on both routes
(`admitsWB_eq` proves that; here it is also observed).

TWO MODES, because a census and an acceptance run are different claims:

  * DEFAULT (census): report the two answers and check only the invariants that
    must hold whatever the answers are -- the original must never admit a plain
    write, and a design with NO plain write must get the same answer on both
    routes (`admitsWB_eq`).  It does NOT require `admitsWB` to be true, so its
    exit 0 is NOT admission evidence.
  * `--expect-admitted` (acceptance): additionally REQUIRE `admitsWB = true` on
    every design that contains a plain write, and fail on an unexpected
    refusal.  An earlier version had no such mode and returned ALL CONTROLS PASS
    even when `admitsWB` was false, which made its exit 0 unable to distinguish
    admission from refusal.

Exit 0 only when every file parsed and every checked condition held.
-/
import LeanSemanticPrimitives.Projection.HardwareMemWriteAdmit
import LeanSemanticPrimitives.Compiler.CertIOFacade

open Compiler Projection Projection.HwMem

def plainWrites (D : DesignCert) : Nat :=
  D.nodes.toList.foldl (fun n c => if c.op == .Op_MemWrite then n + 1 else n) 0

def beWrites (D : DesignCert) : Nat :=
  D.nodes.toList.foldl (fun n c =>
    match c.op with | .Op_MemWriteBE _ => n + 1 | _ => n) 0

/-- The acceptance decision, as a function, so it can be tested without a file.
`true` = this design is a control FAILURE under the given mode. -/
def acceptanceFails (expectAdmitted : Bool) (D : DesignCert) : Bool :=
  let pw := plainWrites D
  let a  := admitsB D
  let aw := admitsWB D
  if pw == 0 then a != aw            -- the extension must not change an existing answer
  else a || (expectAdmitted && !aw)  -- the original must refuse; acceptance needs the extension to admit

/-- SELF-TEST: the acceptance mode must FAIL on a plain-write design the
extended checker refuses, or its exit 0 means nothing.  Both fixtures are built
here, so this needs no artifact. -/
def selftest : IO UInt32 := do
  -- admitted: a mutable table whose next image is the plain write
  let good : DesignCert :=
    { sources  := #[ .memImg 0 2 8, .input 0 2, .input 1 8, .input 2 4 ]
      nodes    := #[ { op := .Op_MemWrite, width := 8, deps := #[0,1,2,3], origin := 0 } ]
      outputs  := #[], flops := #[]
      memories := #[ { aw := 2, dw := 8, nextImg := 4, clock := 0 } ]
      clocks   := #[ { name := "clk" } ] }
  -- refused: identical but the datum is NARROWER than dw, which the typing rejects
  let bad : DesignCert :=
    { good with sources := #[ .memImg 0 2 8, .input 0 2, .input 1 4, .input 2 4 ] }
  -- (no accumulator: each check prints, and the gate below re-evaluates them)
  let chk (what : String) (cond : Bool) : IO Unit := do
    if cond then IO.println s!"  PASS  {what}" else do IO.eprintln s!"  FAIL  {what}"
  IO.println "selftest:"
  chk "the refused design really is refused by the extended checker" (admitsWB bad == false)
  chk "the admitted design really is admitted"                       (admitsWB good == true)
  chk "the ORIGINAL checker refuses both"             (admitsB good == false && admitsB bad == false)
  chk "ACCEPTANCE mode FAILS on the refused design"   (acceptanceFails true  bad == true)
  chk "ACCEPTANCE mode passes the admitted design"    (acceptanceFails true  good == false)
  chk "CENSUS mode tolerates the refused design"      (acceptanceFails false bad == false)
  if admitsWB bad == false && admitsWB good == true && admitsB good == false
     && admitsB bad == false && acceptanceFails true bad == true
     && acceptanceFails true good == false && acceptanceFails false bad == false then do
    IO.println "SELFTEST PASS"; return 0
  else do
    IO.eprintln "SELFTEST FAILED"; return 1

def main (args : List String) : IO UInt32 := do
  if args.contains "--selftest" then return (← selftest)
  let expectAdmitted := args.contains "--expect-admitted"
  let paths := args.filter (fun a => !a.startsWith "--")
  let mode := if expectAdmitted then "ACCEPTANCE (admitsWB must be true on a plain write)"
                                else "CENSUS (admitsWB is reported, not required)"
  IO.println s!"mode: {mode}"
  let mut bad := 0
  let mut plainSeen := 0
  let mut plainAdmitted := 0
  for path in paths do
    let bytes ← IO.FS.readBinFile path
    match CertIO.admitAny bytes with
    | .error e => do IO.eprintln s!"PARSE FAILED {path}: {e}"; bad := bad + 1
    | .ok (_, D) =>
      let pw := plainWrites D
      let be := beWrites D
      let a  := admitsB D
      let aw := admitsWB D
      IO.println s!"{path}"
      IO.println s!"  nodes {D.nodes.size}  memories {D.memories.size}  Op_MemWrite {pw}  Op_MemWriteBE {be}"
      IO.println s!"  admitsB (ORIGINAL, unchanged) = {a}"
      IO.println s!"  admitsWB (EXTENDED)           = {aw}"
      if pw == 0 then
        if a != aw then do
          IO.eprintln "  DISAGREEMENT on a design with NO plain write -- the extension changed an existing answer"
          bad := bad + 1
        else
          IO.println "  no plain write: both routes agree, as admitsWB_eq requires"
      else do
        plainSeen := plainSeen + 1
        if aw then plainAdmitted := plainAdmitted + 1
        if a then do
          IO.eprintln "  the ORIGINAL checker admitted a plain write -- its contract has been weakened"
          bad := bad + 1
        else
          IO.println s!"  {pw} plain write(s): original refuses (contract intact), extended answers {aw}"
        if expectAdmitted && !aw then do
          IO.eprintln "  EXPECTED ADMISSION, GOT REFUSAL: the extended checker refused a plain-write design"
          bad := bad + 1
  IO.println s!"summary: {paths.length} file(s); {plainSeen} with a plain write, {plainAdmitted} of those admitted by the extended checker"
  if bad == 0 then do
    IO.println (if expectAdmitted then "ALL ACCEPTANCE CONTROLS PASS" else "ALL CENSUS CONTROLS PASS")
    return 0
  else do
    IO.eprintln s!"{bad} CONTROL(S) FAILED"
    return 1
