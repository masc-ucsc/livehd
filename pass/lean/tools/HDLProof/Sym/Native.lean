import HDLProof.Sym.NativeOps

/-! Direct symbolic execution of dense certificates and compiled residuals.
The traversal preserves cut definitions. A `Stepper` alone is not a proof
backend: checker soundness additionally requires `MemResidual` for the design.
-/

open Projection Compiler Std.Tactic.BVDecide
namespace HDLProof.Sym.Native

def stateParts : SVal → Option (SVal × SVal)
  | .ctor tag [fs, ms] => if tag = StateM.tagStateM then some (fs, ms) else none
  | .conc (.ctor tag [fs, ms]) =>
      if tag = StateM.tagStateM then some (.conc fs, .conc ms) else none
  | _ => none

def edgesOf : SVal → Option ClockEdges
  | .conc v => decEdges v
  | _ => none

def source (inputs flops memories : SVal) : SourceDesc → Option SVal
  | .input idx w => sNth inputs idx >>= resize w
  | .const w v => some (constVal w v)
  | .flopQ idx w => sNth flops idx >>= resize w
  | .flopQAsync idx w ri rv al => do
      let r ← sNth inputs ri
      let q ← sNth flops idx
      if al then select w r q (constVal w rv)
      else select w r (constVal w rv) q
  | .memImg idx _ _ => sNth memories idx
  | .memConst _ dw contents => some (.conc (MemImg.encMemImg (MemImg.ofMemConst dw contents)))

/-- Each node appends exactly one slot and cuts its symbolic leaves. -/
def walk {α : Type} (eval : Array SVal → α → Option SVal) :
    List α → Array SVal → SymSt → Except String (Array SVal × SymSt)
  | [], env, st => .ok (env, st)
  | node :: rest, env, st =>
      match eval env node with
      | none => .error s!"native symbolic operation refused at slot {env.size}"
      | some value =>
          let (value', st') := cutS st value
          walk eval rest (env.push value') st'

theorem walk_extends {α : Type} (eval : Array SVal → α → Option SVal) :
    ∀ (nodes : List α) (env : Array SVal) (st : SymSt) (out : Array SVal) (st' : SymSt),
      walk eval nodes env st = .ok (out, st') → st.Extends st'
  | [], _, _, _, _, h => by cases h; exact .refl _
  | node :: rest, env, st, out, st', h => by
      unfold walk at h
      split at h
      · cases h
      · rename_i value hv
        exact (cutS_extends st value).trans (walk_extends eval rest _ _ _ _ h)

def irNode (env : Array SVal) (node : DenseNodeCert) : Option SVal := do
  let args ← node.deps.toList.mapM (env[·]?)
  evalOp node.op node.width args

def vcBinding (env : Array SVal) (binding : ResidualBinding) : Option SVal :=
  evalExpr env binding.rhs

def flopNext (env : Array SVal) (edges : ClockEdges) (old : SVal) (idx : Nat)
    (f : FlopDesc) : Option SVal := do
  let held ← sNth old idx
  let value ← if fires edges f.clock then do
    let din ← env[f.din]?
    match f.enable with
    | none => resize f.width din
    | some en => select f.width (← env[en]?) din held
    else pure held
  match f.resetPin with
  | none => pure value
  | some r =>
      if fires edges f.clock || f.asyncReset then
        if f.resetActiveLow then select f.width (← env[r]?) value (constVal f.width f.resetValue)
        else select f.width (← env[r]?) (constVal f.width f.resetValue) value
      else pure value

def memoryNext (env : Array SVal) (edges : ClockEdges) (old : SVal) (idx : Nat)
    (m : MemoryDesc) : Option SVal := do
  let held ← match m.asyncImg with
    | some slot => env[slot]?
    | none => sNth old idx
  if m.commits.isEmpty then
    if fires edges m.clock then env[m.nextImg]? else pure held
  else if m.allFire edges then env[m.nextImg]?
  else match m.commits.find? (m.firedIs edges) with
    | some commit => env[commit.nextImg]?
    | none => pure held

def finish (outputs : Array OutputDesc) (flops : Array FlopDesc) (memories : Array MemoryDesc)
    (edges : ClockEdges) (oldFlops oldMemories : SVal) (env : Array SVal) : Option SVal := do
  let os ← outputs.toList.mapM fun o => env[o.slot]? >>= resize o.width
  let fs ← flops.toList.zipIdx |>.mapM fun (f, idx) => flopNext env edges oldFlops idx f
  let ms ← memories.toList.zipIdx |>.mapM fun (m, idx) => memoryNext env edges oldMemories idx m
  pure (.ctor StateM.tagResultM
    [.ctor StateM.tagStateM [sListOf fs, sListOf ms], sListOf os])

def runWith {α : Type} (eval : Array SVal → α → Option SVal)
    (sources : Array SourceDesc) (nodes : List α)
    (outputs : Array OutputDesc) (flops : Array FlopDesc) (memories : Array MemoryDesc)
    (fuel : Nat) (st : SymSt) (sE sI sS : SVal) : Except String (SVal × SymSt) :=
  if fuel < nodes.length then .error "native symbolic node budget exhausted" else
  match edgesOf sE, stateParts sS with
  | some edges, some (oldFlops, oldMemories) =>
      match sources.toList.mapM (source sI oldFlops oldMemories) with
      | none => .error "native symbolic source shape refused"
      | some values =>
          match walk eval nodes values.toArray st with
          | .error message => .error message
          | .ok (env, st') =>
              match finish outputs flops memories edges oldFlops oldMemories env with
              | none => .error "native symbolic state update refused"
              | some result => .ok (cutS st' result)
  | _, _ => .error "native symbolic edge or state shape refused"

theorem runWith_extends {α : Type} {eval : Array SVal → α → Option SVal}
    {sources : Array SourceDesc} {nodes : List α}
    {outputs : Array OutputDesc} {flops : Array FlopDesc} {memories : Array MemoryDesc}
    {fuel : Nat} {st st' : SymSt} {e i s r : SVal}
    (h : runWith eval sources nodes outputs flops memories fuel st e i s = .ok (r, st')) :
    st.Extends st' := by
  unfold runWith at h
  split at h
  · cases h
  · split at h
    · split at h
      · cases h
      · split at h
        · cases h
        · rename_i env st1 hw
          split at h
          · cases h
          · rename_i result hr
            have he := walk_extends eval nodes _ st env st1 hw
            have hc := cutS_extends st1 result
            have hs := congrArg Prod.snd (Except.ok.inj h)
            rw [hs] at hc
            exact he.trans hc
    · cases h

def irStepper (D : DesignCert) : Stepper where
  run := runWith irNode D.sources D.nodes.toList D.outputs D.flops D.memories
  extends_defs := runWith_extends

def ResidualFlopUpdate.asFlop (f : ResidualFlopUpdate) : FlopDesc :=
  { width := f.width, din := f.din, enable := f.enable, resetPin := f.resetPin,
    resetValue := f.resetValue, resetActiveLow := f.resetActiveLow,
    clock := f.clock, asyncReset := f.asyncReset }

def ResidualMemoryUpdate.asMemory (m : ResidualMemoryUpdate) : MemoryDesc :=
  { aw := m.aw, dw := m.dw, nextImg := m.nextImg, clock := m.clock, commits := m.commits,
    init := m.init, asyncImg := m.asyncImg }

def vcStepper (R : ResidualProgram) : Stepper where
  run := runWith vcBinding R.sources R.bindings.toList
    (R.outputs.map fun o => { slot := o.slot, width := o.width })
    (R.flopUpdates.map ResidualFlopUpdate.asFlop)
    (R.memoryUpdates.map ResidualMemoryUpdate.asMemory)
  extends_defs := runWith_extends

end HDLProof.Sym.Native
