import HDLProof.Sym.NativeBuilders

/-! Direct symbolic execution of dense certificates (IR) and compiled residuals (VC).
The traversal preserves cut definitions. `NativeRoute` proves both steppers are proof
routes: `irStepper_sound : MemResidual D (irStepper D)` and
`vcStepper_sound : compileDesign D = .ok R → MemResidual D (vcStepper R)`.
-/

open Projection Compiler Std.Tactic.BVDecide
namespace HDLProof.Sym.Native

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

/-- The compiled bindings' own dispatch: one case per residual constructor, over the shared
builders. -/
def evalExpr (env : Array SVal) : ResidualExpr → Option SVal
  | .rsum w n args => args.toList.mapM (env[·]?) >>= sumB w n
  | .rmult w args => args.toList.mapM (env[·]?) >>= productB w
  | .rand w args => args.toList.mapM (env[·]?) >>= fun
      | [] => some (constVal w 0)
      | a :: rest => resize w a >>= fun x => foldBits .and w x rest
  | .rorBits w args => args.toList.mapM (env[·]?) >>= foldBits .or w (constVal w 0)
  | .rxor w args => args.toList.mapM (env[·]?) >>= foldBits .xor w (constVal w 0)
  | .rredOr w args => args.toList.mapM (env[·]?) >>= redOrB w
  | .req w args => args.toList.mapM (env[·]?) >>= eqAllB w
  | .rshl w args => args.toList.mapM (env[·]?) >>= fun
      | [] => some (constVal w 0)
      | a :: shifts => do
          let shifted ← shifts.mapM fun b => symBvShl [widthVal w, a, b]
          foldBits .xor w (constVal w 0) shifted
  | .rmuxN w args => args.toList.mapM (env[·]?) >>= fun
      | [] => none
      | sel :: vals => muxNB w sel vals
  | .rnot w a => do symBvNot [widthVal w, ← env[a]?]
  | .rult w a b => do compare false false w (← env[a]?) (← env[b]?)
  | .rugt w a b => do compare false true w (← env[a]?) (← env[b]?)
  | .rslt w a b => do compare true false w (← env[a]?) (← env[b]?)
  | .rsgt w a b => do compare true true w (← env[a]?) (← env[b]?)
  | .rsra w a b => do symBvSra [widthVal w, ← env[a]?, ← env[b]?]
  | .rsext w a n => do sextB w (← env[a]?) (← env[n]?)
  | .rgetMask w a m => do symBvGetMask [widthVal w, ← env[a]?, ← env[m]?]
  | .rsetMask w a m v => do setMaskB w (← env[a]?) (← env[m]?) (← env[v]?)
  | .rmux w sel fv tv => do select w (← env[sel]?) (← env[tv]?) (← env[fv]?)
  | .rmemRead w m a en => do symMemRead [widthVal w, ← env[m]?, ← env[a]?, ← env[en]?]
  | .rmemWrite m a d en => do symMemWrite [← env[m]?, ← env[a]?, ← env[d]?, ← env[en]?]
  | .rmemWriteBE w bw m a d be => do
      symMemWriteBE [widthVal w, ← env[m]?, ← env[a]?, ← env[d]?, ← env[be]?, widthVal bw]
  | .rmemLoad w n m bus en => do memLoadB w n (← env[m]?) (← env[bus]?) (← env[en]?)
  | .rmemToBus w n m => do memToBusB w n (← env[m]?)
  | .rudiv w a b => do udivB w (← env[a]?) (← env[b]?)
  | .rsdiv w a b => do sdivB w (← env[a]?) (← env[b]?)

def vcBinding (env : Array SVal) (binding : ResidualBinding) : Option SVal :=
  evalExpr env binding.rhs

/-- A flop's value before its reset: the (enabled) data on a firing clock, else the held value. -/
def flopValue (env : Array SVal) (edges : ClockEdges) (held : SVal) (f : FlopDesc) :
    Option SVal :=
  if fires edges f.clock then
    match env[f.din]?, f.enable with
    | some din, none => resize f.width din
    | some din, some en =>
        match env[en]? with
        | some c => select f.width c din held
        | none => none
    | none, _ => none
  else some held

/-- The reset applied to the value: on a firing clock or for an asynchronous reset. -/
def flopReset (env : Array SVal) (edges : ClockEdges) (value : SVal) (f : FlopDesc) :
    Option SVal :=
  match f.resetPin with
  | none => some value
  | some r =>
      if fires edges f.clock || f.asyncReset then
        match env[r]? with
        | some c =>
            if f.resetActiveLow then select f.width c value (constVal f.width f.resetValue)
            else select f.width c (constVal f.width f.resetValue) value
        | none => none
      else some value

def flopNext (env : Array SVal) (edges : ClockEdges) (old : SVal) (idx : Nat)
    (f : FlopDesc) : Option SVal := do
  let held ← sNth old idx
  let value ← flopValue env edges held f
  flopReset env edges value f

/-- The image a memory keeps when it does not commit: the async reset's slot, else the stored one. -/
def memHeld (env : Array SVal) (old : SVal) (idx : Nat) (m : MemoryDesc) : Option SVal :=
  match m.asyncImg with
  | some slot => env[slot]?
  | none => sNth old idx

def memoryNext (env : Array SVal) (edges : ClockEdges) (old : SVal) (idx : Nat)
    (m : MemoryDesc) : Option SVal := do
  let held ← memHeld env old idx m
  if m.commits.isEmpty then
    if fires edges m.clock then env[m.nextImg]? else some held
  else if m.allFire edges then env[m.nextImg]?
  else match m.commits.find? (m.firedIs edges) with
    | some commit => env[commit.nextImg]?
    | none => some held

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
