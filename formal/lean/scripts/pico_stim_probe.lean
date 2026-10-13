/-
STIMULUS VALIDATOR for the whole-Pico memory-route runs.

WHY IT EXISTS.  One cycle of `dcert2_memroute` on Pico costs about 168 s,
because it evaluates a 4.67-million-term residual AND the reference.  Asking
"does this stimulus ever make the register file commit?" does not need the
residual at all -- it is a question about `Compiler.interpretDesign`, which is
the same reference the driver compares against.  This probe answers it in
minutes so a stimulus is validated BEFORE an hour of specialization is spent on
it, and it reports the FIRST cycle at which the memory changes.

WHAT IT IS NOT.  It runs the reference only.  It is a diagnostic, not a route:
it specializes nothing, checks no residual and earns no readiness credit.  The
driver remains the thing that executes the projection.

The stimulus is the one in `scripts/memstate/pico_run_instr.stim`, written out
here rather than parsed, so this file has no copy of the driver's parsers to
drift from: reset LOW for two cycles then HIGH, `mem_ready` reacting to the
previous cycle's `mem_valid`, and `mem_rdata` held at `addi x1, x0, 1`.
-/
import LeanSemanticPrimitives.Projection.HardwareMemRuntime
import LeanSemanticPrimitives.Compiler.CertIOFacade

open Compiler Projection Projection.HwMem

def rampImg (aw dw : Nat) (dflt : Int) : MemImg :=
  ⟨(List.range (2 ^ aw)).map (fun _ => mk_bv dw dflt), mk_bv dw dflt⟩

/-- The supplied image of `pico_rom_image.memstate`, inline. -/
def picoImg : MemImg :=
  let base := rampImg 5 32 4294967295
  let set (m : MemImg) (a : Nat) (v : Int) : MemImg :=
    { m with words := m.words.set a (mk_bv 32 v) }
  set (set (set (set (set (set base 0 19088743) 1 2309737967) 2 305419896)
            3 2882400001) 17 48879) 31 3735928559

/-- `resetn` low for cycles 0-1, then high; `mem_ready` from the previous
cycle's `mem_valid` (output ordinal 8); `mem_rdata` = `addi x1, x0, 1`. -/
def picoInput (prevOuts : Array BV) (k : Nat) : RuntimeInput :=
  #[ mk_bv 1 0                                           -- 0 clk
   , mk_bv 32 0                                          -- 1 irq
   , mk_bv 32 1048723                                    -- 2 mem_rdata
   , bv_resize 1 (prevOuts[8]?.getD (mk_bv 1 0))          -- 3 mem_ready
   , mk_bv 32 0                                          -- 4 pcpi_rd
   , mk_bv 1 0, mk_bv 1 0, mk_bv 1 0                     -- 5..7 pcpi_*
   , mk_bv 1 (if k < 2 then 0 else 1) ]                  -- 8 resetn

def main (args : List String) : IO UInt32 := do
  match args with
  | path :: rest =>
    let cycles := (rest.head?.getD "40").toNat?.getD 40
    let bytes ← IO.FS.readBinFile path
    match CertIO.admitAny bytes with
    | .error e => do IO.eprintln s!"PARSE FAILED: {e}"; return 8
    | .ok (_, D) => do
      IO.println s!"{path}"
      IO.println s!"  sources {D.sources.size} nodes {D.nodes.size} flops {D.flops.size} \
memories {D.memories.size}"
      IO.println s!"  REFERENCE ONLY -- no specialization, no residual, no readiness credit"
      let st0 : StateM := { flops := initFlopsOf D, mems := #[picoImg] }
      unless stateFullOKB D st0 do
        IO.eprintln "  the probe's state does not satisfy the design's policy"
        return 7
      let e := allEdges D
      let mut st := st0
      let mut prev : Array BV := #[]
      let mut firstChange : Option Nat := none
      let mut addrs : List Int := []
      for k in List.range cycles do
        let i := picoInput prev k
        let r := Compiler.interpretDesign D e i (StateM.denote st)
        -- the reference's next memory, read back at every address it holds
        let nextWords := (List.range 32).map (fun a =>
          match r.nextState.mems[0]? with
          | some f => f (Int.ofNat a)
          | none   => mk_bv 32 0)
        let curWords := (List.range 32).map (fun a => picoImg.get (Int.ofNat a))
        if firstChange.isNone && nextWords != curWords then firstChange := some k
        addrs := (r.outputs[1]?.getD (mk_bv 32 0)).value :: addrs
        st := { flops := r.nextState.flops
              , mems  := #[{ words := nextWords, dflt := picoImg.dflt }] }
        prev := r.outputs
      IO.println s!"  mem_addr over {cycles} cycles: \
{String.intercalate "," ((addrs.reverse.take 24).map toString)}"
      match firstChange with
      | some k => IO.println s!"  REGISTER FILE FIRST COMMITS AT CYCLE {k}"
      | none   => IO.println s!"  register file NEVER commits in {cycles} cycles"
      return 0
  | _ => do IO.eprintln "usage: pico_stim_probe <cert.dcert> [cycles]"; return 2
