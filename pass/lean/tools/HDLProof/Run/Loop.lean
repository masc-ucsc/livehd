/-
# One cycle of a closed-loop run

`cycleStep` is one cycle, as a pure function. The driver (`drivers/cert_run.lean`) iterates it
and does the I/O (traces, VCD, checkpoints, lockstep).

1. The inputs: the stimulus's for this cycle (or all zero), then the bind file's constants and
   reset, then every bus's `ready = 0`, `rdata = 0`. This is the GUESS.
2. Step on the guess. Its outputs are the outputs before the edge.
3. After reset: stop before the edge on a `stop-output`. For each bus, a raised `valid` starts a
   request (its wait drawn from the bus's latency), or must repeat the pending one
   (`protocol` otherwise); a fetch of a `break` address stops before the edge.
4. A request whose wait has run out completes now: the inputs get `ready = 1` and, for a read,
   the word on `rdata`. An address that names nothing is a `bus-error` (before the edge).
5. When a request completed, step again on the final inputs. The outputs must equal the
   guess's: the host decided on them. A design whose outputs depend on `ready` or `rdata` in
   the same cycle stops with `mealy`.
6. Commit: the next state, and each completed write (memory, console, `exit`, a register). A
   write to a `rom` region stops with `rom-write`, a write to `exit` with `exit`, and a data
   access to a `watch` word with `watchpoint`, all after the edge.

`applied` says whether the cycle's edge was committed. Statuses stop the run; `none` continues.
-/
import HDLProof.Run.Host
import HDLProof.Run.Stim
import HDLProof.Run.Backend

namespace HDLProof
namespace Run

open Compiler Compiler.Direct

structure Ctx where
  D       : DesignCert
  bind    : Bind
  stim    : Option Stim := none
  edges   : EdgeSpec := .all
  step    : ClockEdges → RuntimeInput → RuntimeState → Except String RuntimeResult
  /-- the declared width of each input ordinal, `0 .. maxInput D - 1` -/
  inW     : Array Nat
  /-- `stim` as tables (`StimTab.of`), when there is one: the same inputs, built once -/
  stimTab : Option StimTab := none

def Ctx.mk' (D : DesignCert) (bind : Bind) (stim : Option Stim) (edges : EdgeSpec)
    (st : Stepper) : Ctx :=
  { D, bind, stim, edges, step := st.step,
    inW := (Array.range (maxInput D)).map (inputWidth D),
    stimTab := stim.map (StimTab.of D) }

structure LoopState where
  s     : RuntimeState
  host  : HostState
  cycle : Nat
deriving Inhabited

/-- One bus transfer, for the bus trace. -/
structure BusEvent where
  bus     : Nat
  arrival : Nat
  done    : Nat
  instr   : Bool
  addr    : Nat
  data    : Nat
  strb    : Nat
deriving Inhabited

def BusEvent.kind (e : BusEvent) : Char := if e.instr then 'I' else if e.strb != 0 then 'W' else 'R'

structure CycleOut where
  next    : LoopState
  tick    : Tick
  outs    : Array BV
  events  : Array BusEvent := #[]
  status  : Option String := none
  applied : Bool := true

@[inline] def outVal (outs : Array BV) (k : Nat) : Nat := (bv_uint (outs[k]?.getD (mk_bv 0 0))).toNat

/-- Set input `ord` to `v` at its declared width. -/
def setIn (ctx : Ctx) (i : RuntimeInput) (ord v : Nat) : RuntimeInput :=
  if ord < i.size then i.set! ord (mk_bv (ctx.inW[ord]?.getD 1) (v : Int)) else i

/-- The inputs before any bus answers: the stimulus (or zeros), the constants, the reset, and
every bus's `ready` and `rdata` at 0. -/
def Ctx.guess (ctx : Ctx) (k : Nat) : RuntimeInput := Id.run do
  let mut i : RuntimeInput := match ctx.stimTab, ctx.stim with
    | some t, _ => t.inputAt k
    | none, some st => inputAt ctx.D st k
    | none, none => ctx.inW.map fun w => mk_bv w 0
  for (o, v) in ctx.bind.consts do i := setIn ctx i o v
  if let some (o, act, n) := ctx.bind.reset then
    i := setIn ctx i o (if k < n then act else if act == 0 then 1 else 0)
  for b in ctx.bind.buses do
    i := setIn ctx i b.ready 0
    i := setIn ctx i b.rdata 0
  return i

def Ctx.inReset (ctx : Ctx) (k : Nat) : Bool :=
  match ctx.bind.reset with
  | some (_, _, n) => k < n
  | none => false

/-- The request bus `b` presents in `outs`: address, data, strobes, fetch. -/
def busFields (b : Bus) (outs : Array BV) : Nat × Nat × Nat × Bool :=
  let addr := outVal outs b.addr
  let wdata := (b.wdata.map (outVal outs)).getD 0
  let full := 2 ^ b.bytes - 1
  let strb := match b.wstrb, b.we with
    | some s, _ => outVal outs s % 2 ^ b.bytes
    | none, some w => if outVal outs w != 0 then full else 0
    | none, none => 0
  let instr := (b.instr.map (outVal outs · != 0)).getD false
  (addr, wdata, strb, instr)

/-- What the host does in a cycle, decided from the outputs before the edge: stop before the
edge, or apply `inputs` (the guess with the completed requests answered) and commit `done`. -/
inductive Plan where
  | stop (why : String) (h : HostState)
  | go (inputs : RuntimeInput) (h : HostState) (done : Array (Nat × Pending)) (events : Array BusEvent)

def plan (ctx : Ctx) (st : LoopState) (guess : RuntimeInput) (outs : Array BV) : Plan := Id.run do
  let k := st.cycle
  if ctx.inReset k then return .go guess st.host #[] #[]
  if ctx.bind.stops.any (fun (o, v) => outVal outs o == v) then return .stop "stop-output" st.host
  -- the buses: new requests, protocol, breakpoints, completions
  let mut h := st.host
  let mut done : Array (Nat × Pending) := #[]
  for j in [0:ctx.bind.buses.size] do
    let b := ctx.bind.buses[j]!
    let valid := outVal outs b.valid != 0
    let (addr, wdata, strb, instr) := busFields b outs
    match h.pending[j]? |>.join with
    | some p =>
      unless valid && p.addr == addr && p.wdata == wdata && p.strb == strb && p.instr == instr do
        return .stop "protocol" h
    | none =>
      if valid then
        if instr && ctx.bind.breaks.contains addr then return .stop "breakpoint" h
        let (w, h') := h.drawWait b
        let p : Pending := { arrival := k, wait := w, addr, wdata, strb, instr }
        h := { h' with pending := h'.pending.set! j (some p),
                       stats := { h'.stats with waits := h'.stats.waits + w } }
    if let some p := h.pending[j]? |>.join then
      if k == p.arrival + p.wait then done := done.push (j, p)
  -- answer the completed requests
  let mut inputs := guess
  let mut events : Array BusEvent := #[]
  for (j, p) in done do
    let b := ctx.bind.buses[j]!
    let wa := p.addr - p.addr % b.bytes
    if let .none := ctx.bind.target wa b.bytes then return .stop "bus-error" h
    let rv := if p.strb != 0 then 0 else (h.read ctx.bind wa b.bytes).getD 0
    inputs := setIn ctx (setIn ctx inputs b.ready 1) b.rdata rv
    events := events.push { bus := j, arrival := p.arrival, done := k, instr := p.instr, addr := p.addr,
                            data := if p.strb != 0 then p.wdata else rv, strb := p.strb }
  return .go inputs h done events

/-- Commit the completed requests: writes, statistics, and the stop they cause. -/
def commit (ctx : Ctx) (h : HostState) (done : Array (Nat × Pending)) : HostState × Option String :=
  Id.run do
  let mut h := h
  let mut status : Option String := none
  for (j, p) in done do
    let b := ctx.bind.buses[j]!
    let wa := p.addr - p.addr % b.bytes
    h := { h with pending := h.pending.set! j none }
    if p.strb != 0 then
      let (h', stop) := h.write ctx.bind wa b.bytes p.wdata p.strb
      h := { h' with stats := { h'.stats with writes := h'.stats.writes + 1 } }
      if status.isNone then status := stop
    else if p.instr then h := { h with stats := { h.stats with fetches := h.stats.fetches + 1 } }
    else h := { h with stats := { h.stats with reads := h.stats.reads + 1 } }
    if !p.instr && ctx.bind.watches.contains wa && status.isNone then status := some "watchpoint"
  return (h, status)

/-- The step on the final inputs: the guess's result when no request completed. -/
def stepFinal (ctx : Ctx) (e : ClockEdges) (s : RuntimeState) (guess : RuntimeInput)
    (rg : RuntimeResult) (inputs : RuntimeInput) : Except String RuntimeResult :=
  if inputs == guess then .ok rg else ctx.step e inputs s

/-- One cycle. `.error` only when the stepper refuses (the run's status is then `refused`). -/
def cycleStep (ctx : Ctx) (st : LoopState) : Except String CycleOut :=
  let e := edgesAt ctx.D ctx.edges st.cycle
  let guess := ctx.guess st.cycle
  match ctx.step e guess st.s with
  | .error err => .error err
  | .ok rg =>
    match plan ctx st guess rg.outputs with
    | .stop why h =>
      .ok { next := { st with host := h }, tick := { edges := e, input := guess }, outs := rg.outputs,
            status := some why, applied := false }
    | .go inputs h done events =>
      match stepFinal ctx e st.s guess rg inputs with
      | .error err => .error err
      | .ok r =>
        if r.outputs == rg.outputs then
          .ok { next := { s := r.nextState, host := (commit ctx h done).1, cycle := st.cycle + 1 },
                tick := { edges := e, input := inputs }, outs := r.outputs, events,
                status := (commit ctx h done).2 }
        else
          .ok { next := { st with host := h }, tick := { edges := e, input := inputs },
                outs := rg.outputs, status := some "mealy", applied := false }

end Run
end HDLProof
