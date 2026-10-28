/-
STATIC trace of a certificate's memory-write node.

The 40-cycle reference probe showed Pico's PC advancing 0,4,8,12,16 -- the core
executes -- while the declared aw5/dw32 memory never commits.  The next
question is which slot drives that write's ENABLE, and that is a question about
the certificate's STRUCTURE, not about any execution: it costs nothing to
answer and no simulation is run here.
-/
import LeanSemanticPrimitives.Projection.HardwareMemRuntime
import LeanSemanticPrimitives.Compiler.CertIOFacade

open Compiler Projection Projection.HwMem

def srcStr : SourceDesc → String
  | .input idx w            => s!"input {idx} (w {w})"
  | .const w v              => s!"const {v} (w {w})"
  | .flopQ idx w            => s!"flopQ {idx} (w {w})"
  | .flopQAsync idx w ri _ _ => s!"flopQAsync {idx} (w {w}, reset from input {ri})"
  | .memImg i aw dw         => s!"memImg {i} (aw {aw} dw {dw})"
  | .memConst aw dw ct      => s!"memConst (aw {aw} dw {dw}, {ct.size} words)"

/-- What a slot IS: a source descriptor, or a node's operator. -/
def slotStr (D : DesignCert) (k : Nat) : String :=
  if h : k < D.sources.size then s!"slot {k} = source {srcStr (D.sources[k]'h)}"
  else
    match D.nodes[k - D.sources.size]? with
    | some c => s!"slot {k} = node {k - D.sources.size}: {repr c.op} w{c.width} deps {c.deps.toList}"
    | none   => s!"slot {k} = OUT OF RANGE"

def main (args : List String) : IO UInt32 := do
  match args with
  | path :: _ => do
    let bytes ← IO.FS.readBinFile path
    match CertIO.admitAny bytes with
    | .error e => do IO.eprintln s!"PARSE FAILED: {e}"; return 8
    | .ok (_, D) => do
      IO.println s!"{path}"
      IO.println s!"  sources {D.sources.size} nodes {D.nodes.size} memories {D.memories.size}"
      for (md, k) in D.memories.toList.zipIdx do
        IO.println s!"  memory {k}: aw {md.aw} dw {md.dw} clock {md.clock} nextImg slot {md.nextImg}"
        IO.println s!"    {slotStr D md.nextImg}"
        match D.nodes[md.nextImg - D.sources.size]? with
        | some c =>
            for (d, p) in c.deps.toList.zipIdx do
              let role := match p with
                          | 0 => "mem" | 1 => "addr" | 2 => "data" | _ => "ENABLE"
              IO.println s!"    operand {p} ({role}) -> {slotStr D d}"
              -- one more level, for an enable driven by a node
              if p >= 3 then
                -- walk the enable cone a few levels; a 32-bit byte-enable with
                -- byte_w = 1 is a per-BIT write mask, so what it is matters
                let mut frontier : List (Nat × Nat) := [(d, 0)]
                let mut seen : List Nat := []
                for _ in List.range 64 do
                  match frontier with
                  | [] => pure ()
                  | (x, lvl) :: rest =>
                      frontier := rest
                      if lvl > 3 || seen.contains x then pure ()
                      else
                        seen := x :: seen
                        let pad := String.join (List.replicate (lvl + 3) "  ")
                        IO.println s!"{pad}{slotStr D x}"
                        if x >= D.sources.size then
                          match D.nodes[x - D.sources.size]? with
                          | some e => frontier := frontier ++ e.deps.toList.map (fun z => (z, lvl + 1))
                          | none   => pure ()
        | none => IO.println "    (next image is not a node)"
      return 0
  | _ => do IO.eprintln "usage: pico_write_trace <cert.dcert>"; return 2
