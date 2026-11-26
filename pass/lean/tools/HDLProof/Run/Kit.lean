/-
# The run kit: what a simulation driver does besides stepping

`cert_run` (the closed loop) and `traceRun` (`Run/Trace`: the open loop of `cert_trace` and of
the per-design TV and legacy drivers) start, checkpoint and compact a run the same way. These
are the one copy of each; a driver keeps its own wording of a refusal and its own exit codes.

* The start state (`startState`): `init:zero` is `Direct.zeroState`, otherwise the power-on
  state `Direct.resetState`.
* The design hash (`designHashLazy`): `Ckpt.designHash` renders the whole certificate (3.9 s
  and 800 MB on SoomRV's SoC), so a run computes it only when it reads or writes a checkpoint.
  A compaction's checkpoint never leaves memory, and `restore` ignores its design field
  (`compacted_outputs` holds for any hash), so compaction does not need it.
* A start checkpoint (`readCkpt`): parsed, then validated against the certificate
  (`Ckpt.validate`: design hash, shapes, values). The two refusals stay apart, since drivers
  give them different exit codes.
* The checkpoints a run writes: each `ckpt-at:<k>:<file>` inside the run (`ckptAtOutside`), and
  the bytes (`writeCkpt`), the same for every route.
* Automatic compaction (`autoCompact`): every `autoCompactK` cycles on a run longer than that,
  of a design with memories, when the certificate meets `compacted_outputs`' hypotheses
  (`compactBlocker`).
-/
import LeanSemanticPrimitives.Compiler.Ckpt
import LeanSemanticPrimitives.Compiler.DirectTrace
import LeanSemanticPrimitives.Compiler.MemAddr

namespace HDLProof
namespace Run

open Compiler

/-- The start state: all zero (`init:zero`) or the power-on state. -/
def startState (D : DesignCert) (zero : Bool) : RuntimeState :=
  if zero then Direct.zeroState D else Direct.resetState D

/-- The design hash, computed on its first use. -/
def designHashLazy (D : DesignCert) : Thunk Nat := Thunk.mk fun _ => Ckpt.designHash D

/-- Why a start checkpoint was refused: its bytes do not parse, or it does not belong to the
certificate. Each carries the message, which starts with the file's name. -/
inductive CkptRefusal where
  | parse (msg : String)
  | invalid (msg : String)

def CkptRefusal.msg : CkptRefusal → String
  | .parse m | .invalid m => m

/-- Read the checkpoint at `p` and validate it against `D` (its hash `hash`). -/
def readCkpt (D : DesignCert) (hash : Thunk Nat) (p : String) : IO (Except CkptRefusal Ckpt.Ckpt) := do
  match Ckpt.parse (← IO.FS.readBinFile p) with
  | .error e => return .error (.parse s!"{p}: {e}")
  | .ok c =>
    match Ckpt.validate D hash.get c with
    | some e => return .error (.invalid s!"{p}: {e}")
    | none => return .ok c

/-- The first `ckpt-at` cycle outside a run of `cycles` cycles from cycle `start`, as the message
both loops print. -/
def ckptAtOutside (ckptAt : Array (Nat × String)) (start cycles : Nat) : Option String :=
  (ckptAt.find? fun (k, _) => !(start ≤ k && k ≤ start + cycles)).map fun (k, p) =>
    s!"ckpt-at:{k}:{p}: cycle {k} is outside this run ({start} .. {start + cycles})"

/-- Write the checkpoint of state `s` at cycle `k` (with ext sections `exts`) to `p`. -/
def writeCkpt (D : DesignCert) (hash : Thunk Nat) (p : String) (k : Nat) (exts : Array Ckpt.Ext)
    (s : RuntimeState) : IO Unit :=
  IO.FS.writeBinFile p (Ckpt.render (Ckpt.save D hash.get k exts s))

/-- The period of automatic compaction, in cycles. Measured on the census designs: a
compaction costs 14-70 ms, 3-15 cycles' worth, so every 256 cycles it adds 1.4-5% and keeps
the time per cycle flat. -/
def autoCompactK : Nat := 256

/-- Why compaction is not covered by `compacted_outputs` on this certificate, or `none`: the
theorem's hypotheses, checked (`checkDesign_sound` gives `DesignSemWF`). -/
def compactBlocker (D : DesignCert) : Option String :=
  match Compiler.Direct.checkDesign D with
  | .error e => some s!"checkDesign: {e.render}"
  | .ok _ => (MemAddr.addrError D).orElse fun _ =>
      (Ckpt.imgRootError D).orElse fun _ => Ckpt.wordError D

/-- Automatic compaction for a run of up to `cycles` cycles of `D`: `none` when it does not
apply (a run that does not pass `autoCompactK`, a design without memories), else the period
`autoCompactK`, or why the certificate keeps it off (`compactBlocker`), which the driver prints. -/
def autoCompact (D : DesignCert) (cycles : Nat) : Option (Except String Nat) :=
  if cycles > autoCompactK && D.memories.size > 0 then
    some (match compactBlocker D with
      | none => .ok autoCompactK
      | some why => .error why)
  else none

end Run
end HDLProof
